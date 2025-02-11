import click
import sys
import time
import traceback
from pathlib import Path

from yt.wrapper import YtClient
from yt.common import YtResponseError

from . import common


@common.cli.command()
@click.option("--stage", default="production", show_default=True, help="Stage of YQL agent.")
@click.option(
    "--token",
    envvar="YT_TOKEN",
    help="YT token. Fetched from file ~/.yt/token or from env var YT_TOKEN by default. "
    "See https://ytsaurus.tech/docs/user-guide/storage/auth for more information on how to get your token.",
)
@click.option("--title-prefix", default="[QT] ", show_default=True, help="Title prefix of YQL query on query tracker")
@common.run_options
def qt(
    queries: list[int],
    optimized: bool | None,
    query_path: str,
    optimized_path: str,
    query_source: common.QuerySource,
    proxy: str,
    pragma_add: list[str],
    pragma_file: str | None,
    pragma_preset: list[str],
    poller_interval: str,
    stage: str,
    token: str | None,
    timeout: int,  # seconds
    artifact_path: str | None,
    title_prefix: str,
) -> None:
    """Run TPC-DS benchmark queries using the Query Tracker."""

    arguments = locals()

    if token is None:
        try:
            with open(Path.home() / ".yt" / "token", "r") as f:
                token = f.read().strip()
        except FileNotFoundError:
            pass

    if token is None:
        raise RuntimeError("YT token is not specified")

    runnable_queries = common.get_runnable_queries(queries, optimized, query_path, optimized_path, query_source, pragma_add, pragma_file, pragma_preset)

    with common.ArtifactLogger(artifact_path) as logger:
        logger.dump_launch(arguments)

        client = YtClient(proxy=proxy, token=token)
        for runnable in runnable_queries:
            query_id = None
            try:
                logger.start_query(runnable)
                query = common.make_query(runnable, query_path, optimized_path, query_source)
                logger.dump_query(query)

                settings = {
                    "stage": stage,
                    "poller_interval": poller_interval,
                }

                query_title = runnable.get_title(title_prefix)
                query_id = client.start_query(
                    engine="yql",
                    query=query,
                    settings=settings,
                    annotations={"title": query_title}
                )
                logger.dump_id(query_id)

                query_link = f"https://beta.yt.yandex-team.ru/{proxy}/queries/{query_id}"

                print(query_id, flush=True)
                print(f"Query {query_title} link: {query_link}", file=sys.stderr)

                start_time = time.time()

                state = client.get_query(query_id)["state"]
                while state == "pending" or state == "running":
                    if time.time() - start_time >= timeout:
                        client.abort_query(query_id)
                        state = "aborted"
                    else:
                        time.sleep(5)
                        state = client.get_query(query_id)["state"]
                print(f"Query {query_title} finished with state: {state}", file=sys.stderr)
                query_info = client.get_query(query_id)
                logger.dump_info(query_info)
            except Exception as err:
                print(f"Error while running query {query_title}: {err}", file=sys.stderr)
                print(traceback.format_exc(), file=sys.stderr)
                logger.dump_error({
                    "error": str(err),
                    "traceback": traceback.format_exc(),
                })
            except KeyboardInterrupt:
                if query_id is not None:
                    try:
                        client.abort_query(query_id)
                    except YtResponseError:
                        pass
                raise
