#pragma once

#include <yt/cpp/mapreduce/http/context.h>

#include <yt/cpp/mapreduce/interface/client_method_options.h>
#include <yt/cpp/mapreduce/interface/raw_client.h>

#include <yt/yt/client/api/public.h>

namespace NYT::NDetail {

////////////////////////////////////////////////////////////////////////////////

class TRpcRawClient
    : public IRawClient
{
public:
    TRpcRawClient(
        NApi::IClientPtr client,
        const TClientContext& context);

    // Cypress

    TNode Get(
        const TTransactionId& transactionId,
        const TYPath& path,
        const TGetOptions& options = {}) override;

    void Set(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& path,
        const TNode& value,
        const TSetOptions& options = {}) override;

    bool Exists(
        const TTransactionId& transactionId,
        const TYPath& path,
        const TExistsOptions& options = {}) override;

    void MultisetAttributes(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& path,
        const TNode::TMapType& value,
        const TMultisetAttributesOptions& options = {}) override;

    TNodeId Create(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& path,
        const ENodeType& type,
        const TCreateOptions& options = {}) override;

    TNodeId CopyWithoutRetries(
        const TTransactionId& transactionId,
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TCopyOptions& options = {}) override;

    TNodeId CopyInsideMasterCell(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TCopyOptions& options = {}) override;

    TNodeId MoveWithoutRetries(
        const TTransactionId& transactionId,
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TMoveOptions& options = {}) override;

    TNodeId MoveInsideMasterCell(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TMoveOptions& options = {}) override;

    void Remove(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& path,
        const TRemoveOptions& options = {}) override;

    TNode::TListType List(
        const TTransactionId& transactionId,
        const TYPath& path,
        const TListOptions& options = {}) override;

    TNodeId Link(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& targetPath,
        const TYPath& linkPath,
        const TLinkOptions& options = {}) override;

    TLockId Lock(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& path,
        ELockMode mode,
        const TLockOptions& options = {}) override;

    void Unlock(
        TMutationId& mutationId,
        const TTransactionId& transactionId,
        const TYPath& path,
        const TUnlockOptions& options = {}) override;

private:
    const NApi::IClientPtr Client_;
    const TClientContext Context_;
};

////////////////////////////////////////////////////////////////////////////////

}
