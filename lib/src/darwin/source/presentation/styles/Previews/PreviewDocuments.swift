#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum StylesDocuments {
    /// tests/data/protocol/Fixtures/markdown-answer.jsonl
    static let markdownAnswer = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"chatty","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"## Timeo","turn":1,"type":"answer_delta"}
{"text":"uts that","turn":1,"type":"answer_delta"}
{"text":" keep th","turn":1,"type":"answer_delta"}
{"text":"e batch ","turn":1,"type":"answer_delta"}
{"text":"going\n\nC","turn":1,"type":"answer_delta"}
{"text":"atch the","turn":1,"type":"answer_delta"}
{"text":" timeout","turn":1,"type":"answer_delta"}
{"text":" **insid","turn":1,"type":"answer_delta"}
{"text":"e** the ","turn":1,"type":"answer_delta"}
{"text":"child, n","turn":1,"type":"answer_delta"}
{"text":"ot aroun","turn":1,"type":"answer_delta"}
{"text":"d the gr","turn":1,"type":"answer_delta"}
{"text":"oup. The","turn":1,"type":"answer_delta"}
{"text":" `TaskGr","turn":1,"type":"answer_delta"}
{"text":"oup` sta","turn":1,"type":"answer_delta"}
{"text":"ys *all-","turn":1,"type":"answer_delta"}
{"text":"or-nothi","turn":1,"type":"answer_delta"}
{"text":"ng* for ","turn":1,"type":"answer_delta"}
{"text":"real fai","turn":1,"type":"answer_delta"}
{"text":"lures, a","turn":1,"type":"answer_delta"}
{"text":"nd a slo","turn":1,"type":"answer_delta"}
{"text":"w host j","turn":1,"type":"answer_delta"}
{"text":"ust logs","turn":1,"type":"answer_delta"}
{"text":" and mov","turn":1,"type":"answer_delta"}
{"text":"es on:\n\n","turn":1,"type":"answer_delta"}
{"text":"```pytho","turn":1,"type":"answer_delta"}
{"text":"n\nasync ","turn":1,"type":"answer_delta"}
{"text":"def _fet","turn":1,"type":"answer_delta"}
{"text":"ch(self,","turn":1,"type":"answer_delta"}
{"text":" url: st","turn":1,"type":"answer_delta"}
{"text":"r) -> by","turn":1,"type":"answer_delta"}
{"text":"tes | No","turn":1,"type":"answer_delta"}
{"text":"ne:\n    ","turn":1,"type":"answer_delta"}
{"text":"try:\n   ","turn":1,"type":"answer_delta"}
{"text":"     asy","turn":1,"type":"answer_delta"}
{"text":"nc with ","turn":1,"type":"answer_delta"}
{"text":"asyncio.","turn":1,"type":"answer_delta"}
{"text":"timeout(","turn":1,"type":"answer_delta"}
{"text":"10):\n   ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":" return ","turn":1,"type":"answer_delta"}
{"text":"await se","turn":1,"type":"answer_delta"}
{"text":"lf.clien","turn":1,"type":"answer_delta"}
{"text":"t.get(ur","turn":1,"type":"answer_delta"}
{"text":"l)\n    e","turn":1,"type":"answer_delta"}
{"text":"xcept Ti","turn":1,"type":"answer_delta"}
{"text":"meoutErr","turn":1,"type":"answer_delta"}
{"text":"or:\n    ","turn":1,"type":"answer_delta"}
{"text":"    log.","turn":1,"type":"answer_delta"}
{"text":"warning(","turn":1,"type":"answer_delta"}
{"text":"\"timed o","turn":1,"type":"answer_delta"}
{"text":"ut: %s\",","turn":1,"type":"answer_delta"}
{"text":" url)  #","turn":1,"type":"answer_delta"}
{"text":" the bat","turn":1,"type":"answer_delta"}
{"text":"ch keeps","turn":1,"type":"answer_delta"}
{"text":" going\n ","turn":1,"type":"answer_delta"}
{"text":"       r","turn":1,"type":"answer_delta"}
{"text":"eturn No","turn":1,"type":"answer_delta"}
{"text":"ne\n```\n\n","turn":1,"type":"answer_delta"}
{"text":"What cha","turn":1,"type":"answer_delta"}
{"text":"nges:\n\n-","turn":1,"type":"answer_delta"}
{"text":" `_fetch","turn":1,"type":"answer_delta"}
{"text":"()` retu","turn":1,"type":"answer_delta"}
{"text":"rns `Non","turn":1,"type":"answer_delta"}
{"text":"e` on a ","turn":1,"type":"answer_delta"}
{"text":"timeout\n","turn":1,"type":"answer_delta"}
{"text":"  - the ","turn":1,"type":"answer_delta"}
{"text":"caller s","turn":1,"type":"answer_delta"}
{"text":"kips it\n","turn":1,"type":"answer_delta"}
{"text":"  - the ","turn":1,"type":"answer_delta"}
{"text":"log keep","turn":1,"type":"answer_delta"}
{"text":"s the ad","turn":1,"type":"answer_delta"}
{"text":"dress\n- ","turn":1,"type":"answer_delta"}
{"text":"every ot","turn":1,"type":"answer_delta"}
{"text":"her fail","turn":1,"type":"answer_delta"}
{"text":"ure stil","turn":1,"type":"answer_delta"}
{"text":"l cancel","turn":1,"type":"answer_delta"}
{"text":"s the gr","turn":1,"type":"answer_delta"}
{"text":"oup\n\n1. ","turn":1,"type":"answer_delta"}
{"text":"Run the ","turn":1,"type":"answer_delta"}
{"text":"scraper ","turn":1,"type":"answer_delta"}
{"text":"again.\n2","turn":1,"type":"answer_delta"}
{"text":". Check ","turn":1,"type":"answer_delta"}
{"text":"the log ","turn":1,"type":"answer_delta"}
{"text":"for `tim","turn":1,"type":"answer_delta"}
{"text":"ed out`.","turn":1,"type":"answer_delta"}
{"text":"\n\n> A ca","turn":1,"type":"answer_delta"}
{"text":"ncel fro","turn":1,"type":"answer_delta"}
{"text":"m outsid","turn":1,"type":"answer_delta"}
{"text":"e still ","turn":1,"type":"answer_delta"}
{"text":"arrives ","turn":1,"type":"answer_delta"}
{"text":"as `Canc","turn":1,"type":"answer_delta"}
{"text":"elledErr","turn":1,"type":"answer_delta"}
{"text":"or`, so ","turn":1,"type":"answer_delta"}
{"text":"shutdown","turn":1,"type":"answer_delta"}
{"text":" works.\n","turn":1,"type":"answer_delta"}
{"text":"\n| Optio","turn":1,"type":"answer_delta"}
{"text":"n | Keep","turn":1,"type":"answer_delta"}
{"text":"s the ba","turn":1,"type":"answer_delta"}
{"text":"tch | Ca","turn":1,"type":"answer_delta"}
{"text":"ncels on","turn":1,"type":"answer_delta"}
{"text":" failure","turn":1,"type":"answer_delta"}
{"text":" |\n|---|","turn":1,"type":"answer_delta"}
{"text":":-:|---|","turn":1,"type":"answer_delta"}
{"text":"\n| `asyn","turn":1,"type":"answer_delta"}
{"text":"cio.time","turn":1,"type":"answer_delta"}
{"text":"out` in ","turn":1,"type":"answer_delta"}
{"text":"the chil","turn":1,"type":"answer_delta"}
{"text":"d | yes ","turn":1,"type":"answer_delta"}
{"text":"| yes |\n","turn":1,"type":"answer_delta"}
{"text":"| `gathe","turn":1,"type":"answer_delta"}
{"text":"r(..., r","turn":1,"type":"answer_delta"}
{"text":"eturn_ex","turn":1,"type":"answer_delta"}
{"text":"ceptions","turn":1,"type":"answer_delta"}
{"text":"=True)` ","turn":1,"type":"answer_delta"}
{"text":"| yes | ","turn":1,"type":"answer_delta"}
{"text":"no |\n\n--","turn":1,"type":"answer_delta"}
{"text":"-\n\nSee t","turn":1,"type":"answer_delta"}
{"text":"he [asyn","turn":1,"type":"answer_delta"}
{"text":"cio docs","turn":1,"type":"answer_delta"}
{"text":"](https:","turn":1,"type":"answer_delta"}
{"text":"//docs.p","turn":1,"type":"answer_delta"}
{"text":"ython.or","turn":1,"type":"answer_delta"}
{"text":"g/3/libr","turn":1,"type":"answer_delta"}
{"text":"ary/asyn","turn":1,"type":"answer_delta"}
{"text":"cio-task","turn":1,"type":"answer_delta"}
{"text":".html) o","turn":1,"type":"answer_delta"}
{"text":"r [write","turn":1,"type":"answer_delta"}
{"text":" to the ","turn":1,"type":"answer_delta"}
{"text":"team](ma","turn":1,"type":"answer_delta"}
{"text":"ilto:tea","turn":1,"type":"answer_delta"}
{"text":"m@exampl","turn":1,"type":"answer_delta"}
{"text":"e.com); ","turn":1,"type":"answer_delta"}
{"text":"the note","turn":1,"type":"answer_delta"}
{"text":"s are in","turn":1,"type":"answer_delta"}
{"text":" [runner","turn":1,"type":"answer_delta"}
{"text":".md](fil","turn":1,"type":"answer_delta"}
{"text":"e:///Use","turn":1,"type":"answer_delta"}
{"text":"rs/someo","turn":1,"type":"answer_delta"}
{"text":"ne/Docum","turn":1,"type":"answer_delta"}
{"text":"ents/run","turn":1,"type":"answer_delta"}
{"text":"ner.md).","turn":1,"type":"answer_delta"}
{"text":"\n\n![The ","turn":1,"type":"answer_delta"}
{"text":"batch ti","turn":1,"type":"answer_delta"}
{"text":"meline](","turn":1,"type":"answer_delta"}
{"text":"https://","turn":1,"type":"answer_delta"}
{"text":"example.","turn":1,"type":"answer_delta"}
{"text":"com/time","turn":1,"type":"answer_delta"}
{"text":"line.png","turn":1,"type":"answer_delta"}
{"text":")","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"chatty","text":"## Timeouts that keep the batch going\n\nCatch the timeout **inside** the child, not around the group. The `TaskGroup` stays *all-or-nothing* for real failures, and a slow host just logs and moves on:\n\n```python\nasync def _fetch(self, url: str) -> bytes | None:\n    try:\n        async with asyncio.timeout(10):\n            return await self.client.get(url)\n    except TimeoutError:\n        log.warning(\"timed out: %s\", url)  # the batch keeps going\n        return None\n```\n\nWhat changes:\n\n- `_fetch()` returns `None` on a timeout\n  - the caller skips it\n  - the log keeps the address\n- every other failure still cancels the group\n\n1. Run the scraper again.\n2. Check the log for `timed out`.\n\n> A cancel from outside still arrives as `CancelledError`, so shutdown works.\n\n| Option | Keeps the batch | Cancels on failure |\n|---|:-:|---|\n| `asyncio.timeout` in the child | yes | yes |\n| `gather(..., return_exceptions=True)` | yes | no |\n\n---\n\nSee the [asyncio docs](https://docs.python.org/3/library/asyncio-task.html) or [write to the team](mailto:team@example.com); the notes are in [runner.md](file:///Users/someone/Documents/runner.md).\n\n![The batch timeline](https://example.com/timeline.png)","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/resumed-chat.jsonl
    static let resumedChat = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"chatty","next_turn":6,"protocol_version":1,"type":"session"}
{"text":"utility — restating the follow-up to rank the tools, with helper","turn":6,"type":"tool_status"}
{"turn":6,"type":"thinking"}
{"turn":6,"type":"answer_start"}
{"text":"Nothing ","turn":6,"type":"answer_delta"}
{"text":"else: th","turn":6,"type":"answer_delta"}
{"text":"e beta e","turn":6,"type":"answer_delta"}
{"text":"nds on 2","turn":6,"type":"answer_delta"}
{"text":"026-10-3","turn":6,"type":"answer_delta"}
{"text":"1, and t","turn":6,"type":"answer_delta"}
{"text":"he launc","turn":6,"type":"answer_delta"}
{"text":"h is on ","turn":6,"type":"answer_delta"}
{"text":"2026-11-","turn":6,"type":"answer_delta"}
{"text":"12.","turn":6,"type":"answer_delta"}
{"turn":6,"type":"answer_end"}
{"finish_reason":"stop","model":"chatty","text":"Nothing else: the beta ends on 2026-10-31, and the launch is on 2026-11-12.","turn":6,"type":"result"}
"""#
}
#endif
