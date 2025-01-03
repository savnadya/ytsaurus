#include "bootstrap.h"

#include "chaos_cache_bootstrap.h"
#include "config.h"
#include "master_cache_bootstrap.h"
#include "private.h"
#include "dynamic_config_manager.h"

#include <yt/yt/server/lib/admin/admin_service.h>
#include <yt/yt/server/lib/admin/restart_service.h>

#include <yt/yt/library/coredumper/coredumper.h>

#include <yt/yt/library/disk_manager/hotswap_manager.h>

#include <yt/yt/library/monitoring/http_integration.h>

#include <yt/yt/library/program/helpers.h>

#include <yt/yt/server/lib/cypress_registrar/cypress_registrar.h>
#include <yt/yt/server/lib/cypress_registrar/config.h>

#include <yt/yt/server/lib/misc/address_helpers.h>
#include <yt/yt/server/lib/misc/restart_manager.h>

#include <yt/yt/ytlib/cell_master_client/cell_directory_synchronizer.h>

#include <yt/yt/ytlib/orchid/orchid_service.h>

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/helpers.h>

#include <yt/yt/ytlib/hive/cluster_directory_synchronizer.h>

#include <yt/yt/client/logging/dynamic_table_log_writer.h>

#include <yt/yt/library/coredumper/public.h>

#include <yt/yt/core/bus/tcp/server.h>

#include <yt/yt/core/concurrency/action_queue.h>

#include <yt/yt/core/http/server.h>

#include <yt/yt/core/net/local_address.h>

#include <yt/yt/core/rpc/bus/server.h>

#include <yt/yt/core/ytree/virtual.h>

namespace NYT::NMasterCache {

using namespace NAdmin;
using namespace NApi::NNative;
using namespace NConcurrency;
using namespace NCoreDump;
using namespace NMonitoring;
using namespace NOrchid;
using namespace NYTree;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

static constexpr auto& Logger = MasterCacheLogger;

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
    : public IBootstrap
{
public:
    explicit TBootstrap(TMasterCacheConfigPtr config)
        : Config_(std::move(config))
    {
        if (Config_->AbortOnUnrecognizedOptions) {
            AbortOnUnrecognizedOptions(Logger(), Config_);
        } else {
            WarnForUnrecognizedOptions(Logger(), Config_);
        }
    }

    void Initialize() override
    {
        ControlQueue_ = New<TActionQueue>("Control");

        BIND(&TBootstrap::DoInitialize, this)
            .AsyncVia(GetControlInvoker())
            .Run()
            .Get()
            .ThrowOnError();
    }

    void Run() override
    {
        BIND(&TBootstrap::DoRun, this)
            .AsyncVia(GetControlInvoker())
            .Run()
            .Get()
            .ThrowOnError();
    }

    const TMasterCacheConfigPtr& GetConfig() const override
    {
        return Config_;
    }

    const IConnectionPtr& GetConnection() const override
    {
        return Connection_;
    }

    const NApi::IClientPtr& GetRootClient() const override
    {
        return RootClient_;
    }

    const IMapNodePtr& GetOrchidRoot() const override
    {
        return OrchidRoot_;
    }

    const NRpc::IServerPtr& GetRpcServer() const override
    {
        return RpcServer_;
    }

    const IInvokerPtr& GetControlInvoker() const override
    {
        return ControlQueue_->GetInvoker();
    }

    const NRpc::IAuthenticatorPtr& GetNativeAuthenticator() const override
    {
        return NativeAuthenticator_;
    }

    const TDynamicConfigManagerPtr& GetDynamicConfigManger() const override
    {
        return DynamicConfigManager_;
    }

private:
    const TMasterCacheConfigPtr Config_;

    TActionQueuePtr ControlQueue_;

    NBus::IBusServerPtr BusServer_;
    NRpc::IServerPtr RpcServer_;
    NHttp::IServerPtr HttpServer_;

    IMapNodePtr OrchidRoot_;
    TMonitoringManagerPtr MonitoringManager_;
    ICypressRegistrarPtr CypressRegistrar_;

    NCoreDump::ICoreDumperPtr CoreDumper_;

    IConnectionPtr Connection_;

    NApi::IClientPtr RootClient_;

    NRpc::IAuthenticatorPtr NativeAuthenticator_;

    std::unique_ptr<IBootstrap> MasterCacheBootstrap_;
    std::unique_ptr<IBootstrap> ChaosCacheBootstrap_;

    TDynamicConfigManagerPtr DynamicConfigManager_;

    void DoInitialize()
    {
        BusServer_ = NBus::CreateBusServer(Config_->BusServer);
        RpcServer_ = NRpc::NBus::CreateBusServer(BusServer_);
        HttpServer_ = NHttp::CreateServer(Config_->CreateMonitoringHttpServerConfig());

        if (Config_->CoreDumper) {
            CoreDumper_ = CreateCoreDumper(Config_->CoreDumper);
        }

        NMonitoring::Initialize(
            HttpServer_,
            Config_->SolomonExporter,
            &MonitoringManager_,
            &OrchidRoot_);

        TConnectionOptions connectionOptions;
        connectionOptions.ChaosResidencyCacheMode = EChaosResidencyCacheType::MasterCache;
        Connection_ = NApi::NNative::CreateConnection(Config_->ClusterConnection, connectionOptions);
        Connection_->GetClusterDirectorySynchronizer()->Start();
        Connection_->GetMasterCellDirectorySynchronizer()->Start();

        RootClient_ = Connection_->CreateClient({.User = NSecurityClient::RootUserName});

        NLogging::GetDynamicTableLogWriterFactory()->SetClient(RootClient_);

        {
            TCypressRegistrarOptions options{
                .RootPath = "//sys/master_caches/" + NNet::BuildServiceAddress(
                    NNet::GetLocalHostName(),
                    Config_->RpcPort),
                .OrchidRemoteAddresses = GetLocalAddresses(/*addresses*/ {}, Config_->RpcPort),
                .ExpireSelf = true,
            };
            CypressRegistrar_ = CreateCypressRegistrar(
                std::move(options),
                Config_->CypressRegistrar,
                RootClient_,
                GetControlInvoker());
        }

        NativeAuthenticator_ = NApi::NNative::CreateNativeAuthenticator(Connection_);

        DynamicConfigManager_ = New<TDynamicConfigManager>(this);
        DynamicConfigManager_->SubscribeConfigChanged(BIND_NO_PROPAGATE(&TBootstrap::OnDynamicConfigChanged, Unretained(this)));

        MasterCacheBootstrap_ = CreateMasterCacheBootstrap(this);
        ChaosCacheBootstrap_ = CreateChaosCacheBootstrap(this);

        MasterCacheBootstrap_->Initialize();
        ChaosCacheBootstrap_->Initialize();

        RpcServer_->RegisterService(CreateAdminService(
            GetControlInvoker(),
            CoreDumper_,
            NativeAuthenticator_));

        auto restartManager = New<TRestartManager>(GetControlInvoker());
        RpcServer_->RegisterService(CreateRestartService(
            restartManager,
            GetControlInvoker(),
            MasterCacheLogger(),
            NativeAuthenticator_));

        if (Config_->ExposeConfigInOrchid) {
            SetNodeByYPath(
                OrchidRoot_,
                "/config",
                CreateVirtualNode(ConvertTo<INodePtr>(Config_)));
            SetNodeByYPath(
                OrchidRoot_,
                "/dynamic_config_manager",
                CreateVirtualNode(DynamicConfigManager_->GetOrchidService()));
        }
        SetNodeByYPath(
            OrchidRoot_,
            "/disk_monitoring",
            CreateVirtualNode(NDiskManager::THotswapManager::GetOrchidService()));

        RpcServer_->RegisterService(CreateOrchidService(
            OrchidRoot_,
            GetControlInvoker(),
            NativeAuthenticator_));
    }

    void DoRun()
    {
        DynamicConfigManager_->Start();

        YT_LOG_INFO("Listening for HTTP requests (Port: %v)", Config_->MonitoringPort);
        HttpServer_->Start();

        YT_LOG_INFO("Listening for RPC requests (Port: %v)", Config_->RpcPort);
        RpcServer_->Start();

        CypressRegistrar_->Start({});
    }

    void OnDynamicConfigChanged(
        const TMasterCacheDynamicConfigPtr& /*oldConfig*/,
        const TMasterCacheDynamicConfigPtr& newConfig)
    {
        ReconfigureSingletons(newConfig);
    }
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IBootstrap> CreateBootstrap(TMasterCacheConfigPtr config)
{
    return std::make_unique<TBootstrap>(std::move(config));
}

////////////////////////////////////////////////////////////////////////////////

TBootstrapBase::TBootstrapBase(IBootstrap* bootstrap)
    : Bootstrap_(bootstrap)
{ }

const TMasterCacheConfigPtr& TBootstrapBase::GetConfig() const
{
    return Bootstrap_->GetConfig();
}

const IConnectionPtr& TBootstrapBase::GetConnection() const
{
    return Bootstrap_->GetConnection();
}

const NApi::IClientPtr& TBootstrapBase::GetRootClient() const
{
    return Bootstrap_->GetRootClient();
}

const IMapNodePtr& TBootstrapBase::GetOrchidRoot() const
{
    return Bootstrap_->GetOrchidRoot();
}

const NRpc::IServerPtr& TBootstrapBase::GetRpcServer() const
{
    return Bootstrap_->GetRpcServer();
}

const IInvokerPtr& TBootstrapBase::GetControlInvoker() const
{
    return Bootstrap_->GetControlInvoker();
}

const NRpc::IAuthenticatorPtr& TBootstrapBase::GetNativeAuthenticator() const
{
    return Bootstrap_->GetNativeAuthenticator();
}

const TDynamicConfigManagerPtr& TBootstrapBase::GetDynamicConfigManger() const
{
    return Bootstrap_->GetDynamicConfigManger();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NMasterCache
