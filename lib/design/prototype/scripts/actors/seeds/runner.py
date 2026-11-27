"""The ingest runner: fetch a batch of URLs, parse each page, hand it on."""

import asyncio
import logging

import httpx

from ingest.config import Settings
from ingest.parsers.feed import parse

log = logging.getLogger(__name__)


class Runner:
    """Runs one batch at a time, every URL in it as its own task."""

    def __init__(self, settings: Settings, client: httpx.AsyncClient) -> None:
        self.settings = settings
        self.client = client
        self.pages: list[bytes] = []

    async def run(self, batches: list[list[str]]) -> None:
        """Every batch in order; a batch that fails is logged, and the next runs."""
        for number, batch in enumerate(batches, start=1):
            try:
                await self.run_batch(batch)
            except* TimeoutError as group:
                log.error("batch %d timed out: %s", number, group.exceptions)

    def summary(self) -> str:
        return f"{len(self.pages)} pages fetched"

    # -- one batch --------------------------------------------------------

    def _check(self, batch: list[str]) -> None:
        if not batch:
            raise ValueError("an empty batch")
        if len(batch) > self.settings.ingest.batch_size:
            raise ValueError("a batch over batch_size")

    def _hosts(self, batch: list[str]) -> set[str]:
        return {httpx.URL(url).host for url in batch}

    def _log_start(self, batch: list[str]) -> None:
        log.info("batch of %d urls from %d hosts", len(batch), len(self._hosts(batch)))

    # The batch is one TaskGroup: every URL a task, and the group's exit
    # waits for all of them. The first task that raises cancels the rest
    # (run() is cancelled the same way on shutdown).

    # -- the batch itself --
    async def run_batch(self, batch: list[str]) -> None:
        async with asyncio.TaskGroup() as tg:
            for url in batch:
                tg.create_task(self._fetch(url))
        log.info("batch done: %d urls", len(batch))

    # -- one URL ----------------------------------------------------------

    def _store(self, page: bytes) -> None:
        self.pages.append(page)

    def _parse(self, page: bytes) -> None:
        for item in parse(page):
            log.debug("item: %s", item.title)

    # _fetch() gets one URL; run_batch() makes it a task. A timeout here
    # raises in this task alone, then the TaskGroup cancels its siblings.

    # -- the fetch --
    async def _fetch(self, url: str) -> bytes:
        async with asyncio.timeout(10):
            return await self.client.get(url)

    # -- after the fetch ---------------------------------------------------

    async def fetch_and_store(self, url: str) -> None:
        page = await self._fetch(url)
        self._store(page)
        self._parse(page)

    async def close(self) -> None:
        await self.client.aclose()


async def main(settings: Settings) -> None:
    async with httpx.AsyncClient() as client:
        runner = Runner(settings, client)
        batches = [line.split() for line in open(settings.ingest.source)]
        await runner.run(batches)
        log.info(runner.summary())


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main(Settings.load("config.toml")))
