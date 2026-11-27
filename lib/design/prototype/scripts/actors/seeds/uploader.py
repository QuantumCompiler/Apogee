"""Uploads each fetched page to the archive bucket, with a retry budget (PR 482)."""

import asyncio
import logging

import httpx

from upload.budget import RetryBudget, RetryBudgetExceeded

log = logging.getLogger(__name__)

















































class Uploader:
    """Puts pages in the archive bucket, one request per page."""

    def __init__(self, client: httpx.AsyncClient, bucket: str) -> None:
        self.client = client
        self.bucket = bucket
        self.failed = 0

    async def upload_batch(self, pages: list[tuple[str, bytes]]) -> None:
        """Every page of a batch; a page whose budget runs out is logged and counted."""
        for url, page in pages:
            try:
                await self.upload_one(url, page)
            except RetryBudgetExceeded:
                self.failed += 1
                log.warning("retry budget spent on %s", url)

    def _key(self, url: str) -> str:
        return httpx.URL(url).path.strip("/").replace("/", "-")

    async def _put(self, url: str, page: bytes) -> None:
        response = await self.client.put(f"/{self.bucket}/{self._key(url)}", content=page)
        response.raise_for_status()

    # PR 482: retries are capped by a budget that refills every 60 s.

    async def upload_one(self, url: str, page: bytes) -> None:
        """One page, retried within the budget."""
        budget = RetryBudget(20)
        while True:
            try:
                await self._put(url, page)
                return
            except httpx.TransportError:
                budget.spend()  # raises RetryBudgetExceeded once it is spent
                await asyncio.sleep(1)























































