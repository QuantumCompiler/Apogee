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
    /// tests/data/protocol/Fixtures/code-answer.jsonl
    static let codeAnswer = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"chatty","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"## Retry","turn":1,"type":"answer_delta"}
{"text":"ing a sl","turn":1,"type":"answer_delta"}
{"text":"ow host\n","turn":1,"type":"answer_delta"}
{"text":"\nRetry t","turn":1,"type":"answer_delta"}
{"text":"he timeo","turn":1,"type":"answer_delta"}
{"text":"ut where","turn":1,"type":"answer_delta"}
{"text":" it happ","turn":1,"type":"answer_delta"}
{"text":"ens, and","turn":1,"type":"answer_delta"}
{"text":" keep `a","turn":1,"type":"answer_delta"}
{"text":"syncio.t","turn":1,"type":"answer_delta"}
{"text":"imeout(1","turn":1,"type":"answer_delta"}
{"text":"0)` arou","turn":1,"type":"answer_delta"}
{"text":"nd one a","turn":1,"type":"answer_delta"}
{"text":"ttempt. ","turn":1,"type":"answer_delta"}
{"text":"In the s","turn":1,"type":"answer_delta"}
{"text":"craper:\n","turn":1,"type":"answer_delta"}
{"text":"\n```pyth","turn":1,"type":"answer_delta"}
{"text":"on\nasync","turn":1,"type":"answer_delta"}
{"text":" def fet","turn":1,"type":"answer_delta"}
{"text":"ch(self,","turn":1,"type":"answer_delta"}
{"text":" url: st","turn":1,"type":"answer_delta"}
{"text":"r, retri","turn":1,"type":"answer_delta"}
{"text":"es: int ","turn":1,"type":"answer_delta"}
{"text":"= 3) -> ","turn":1,"type":"answer_delta"}
{"text":"bytes | ","turn":1,"type":"answer_delta"}
{"text":"None:\n  ","turn":1,"type":"answer_delta"}
{"text":"  \"\"\"Fet","turn":1,"type":"answer_delta"}
{"text":"ch url, ","turn":1,"type":"answer_delta"}
{"text":"retrying","turn":1,"type":"answer_delta"}
{"text":" a timeo","turn":1,"type":"answer_delta"}
{"text":"ut.\"\"\"\n ","turn":1,"type":"answer_delta"}
{"text":"   for a","turn":1,"type":"answer_delta"}
{"text":"ttempt i","turn":1,"type":"answer_delta"}
{"text":"n range(","turn":1,"type":"answer_delta"}
{"text":"retries ","turn":1,"type":"answer_delta"}
{"text":"+ 1):\n  ","turn":1,"type":"answer_delta"}
{"text":"      tr","turn":1,"type":"answer_delta"}
{"text":"y:\n     ","turn":1,"type":"answer_delta"}
{"text":"       a","turn":1,"type":"answer_delta"}
{"text":"sync wit","turn":1,"type":"answer_delta"}
{"text":"h asynci","turn":1,"type":"answer_delta"}
{"text":"o.timeou","turn":1,"type":"answer_delta"}
{"text":"t(10.5):","turn":1,"type":"answer_delta"}
{"text":"\n       ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":" return ","turn":1,"type":"answer_delta"}
{"text":"await se","turn":1,"type":"answer_delta"}
{"text":"lf.clien","turn":1,"type":"answer_delta"}
{"text":"t.get(ur","turn":1,"type":"answer_delta"}
{"text":"l)\n     ","turn":1,"type":"answer_delta"}
{"text":"   excep","turn":1,"type":"answer_delta"}
{"text":"t Timeou","turn":1,"type":"answer_delta"}
{"text":"tError:\n","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"    log.","turn":1,"type":"answer_delta"}
{"text":"warning(","turn":1,"type":"answer_delta"}
{"text":"\"attempt","turn":1,"type":"answer_delta"}
{"text":" %d time","turn":1,"type":"answer_delta"}
{"text":"d out: %","turn":1,"type":"answer_delta"}
{"text":"s\", atte","turn":1,"type":"answer_delta"}
{"text":"mpt, url","turn":1,"type":"answer_delta"}
{"text":")  # try","turn":1,"type":"answer_delta"}
{"text":" again\n ","turn":1,"type":"answer_delta"}
{"text":"   retur","turn":1,"type":"answer_delta"}
{"text":"n None\n`","turn":1,"type":"answer_delta"}
{"text":"``\n\nThe ","turn":1,"type":"answer_delta"}
{"text":"app's ow","turn":1,"type":"answer_delta"}
{"text":"n helper","turn":1,"type":"answer_delta"}
{"text":", in Swi","turn":1,"type":"answer_delta"}
{"text":"ft:\n\n```","turn":1,"type":"answer_delta"}
{"text":"swift\n//","turn":1,"type":"answer_delta"}
{"text":"/ Asks a","turn":1,"type":"answer_delta"}
{"text":"gain aft","turn":1,"type":"answer_delta"}
{"text":"er a pau","turn":1,"type":"answer_delta"}
{"text":"se, up t","turn":1,"type":"answer_delta"}
{"text":"o `attem","turn":1,"type":"answer_delta"}
{"text":"pts` tim","turn":1,"type":"answer_delta"}
{"text":"es.\nfunc","turn":1,"type":"answer_delta"}
{"text":" retry<T","turn":1,"type":"answer_delta"}
{"text":">(_ atte","turn":1,"type":"answer_delta"}
{"text":"mpts: In","turn":1,"type":"answer_delta"}
{"text":"t = 3, _","turn":1,"type":"answer_delta"}
{"text":" body: (","turn":1,"type":"answer_delta"}
{"text":") async ","turn":1,"type":"answer_delta"}
{"text":"throws -","turn":1,"type":"answer_delta"}
{"text":"> T) asy","turn":1,"type":"answer_delta"}
{"text":"nc throw","turn":1,"type":"answer_delta"}
{"text":"s -> T {","turn":1,"type":"answer_delta"}
{"text":"\n    for","turn":1,"type":"answer_delta"}
{"text":" attempt","turn":1,"type":"answer_delta"}
{"text":" in 1..<","turn":1,"type":"answer_delta"}
{"text":"attempts","turn":1,"type":"answer_delta"}
{"text":" {\n     ","turn":1,"type":"answer_delta"}
{"text":"   do {\n","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"    retu","turn":1,"type":"answer_delta"}
{"text":"rn try a","turn":1,"type":"answer_delta"}
{"text":"wait bod","turn":1,"type":"answer_delta"}
{"text":"y()\n    ","turn":1,"type":"answer_delta"}
{"text":"    } ca","turn":1,"type":"answer_delta"}
{"text":"tch {\n  ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"  print(","turn":1,"type":"answer_delta"}
{"text":"\"attempt","turn":1,"type":"answer_delta"}
{"text":" \\(attem","turn":1,"type":"answer_delta"}
{"text":"pt) fail","turn":1,"type":"answer_delta"}
{"text":"ed: \\(er","turn":1,"type":"answer_delta"}
{"text":"ror)\")\n ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"   try a","turn":1,"type":"answer_delta"}
{"text":"wait Tas","turn":1,"type":"answer_delta"}
{"text":"k.sleep(","turn":1,"type":"answer_delta"}
{"text":"for: .mi","turn":1,"type":"answer_delta"}
{"text":"llisecon","turn":1,"type":"answer_delta"}
{"text":"ds(250 *","turn":1,"type":"answer_delta"}
{"text":" attempt","turn":1,"type":"answer_delta"}
{"text":"))\n     ","turn":1,"type":"answer_delta"}
{"text":"   }\n   ","turn":1,"type":"answer_delta"}
{"text":" }\n    r","turn":1,"type":"answer_delta"}
{"text":"eturn tr","turn":1,"type":"answer_delta"}
{"text":"y await ","turn":1,"type":"answer_delta"}
{"text":"body()\n}","turn":1,"type":"answer_delta"}
{"text":"\n```\n\nTh","turn":1,"type":"answer_delta"}
{"text":"e browse","turn":1,"type":"answer_delta"}
{"text":"r client","turn":1,"type":"answer_delta"}
{"text":":\n\n```ts","turn":1,"type":"answer_delta"}
{"text":"\n// Back","turn":1,"type":"answer_delta"}
{"text":" off bet","turn":1,"type":"answer_delta"}
{"text":"ween att","turn":1,"type":"answer_delta"}
{"text":"empts.\ne","turn":1,"type":"answer_delta"}
{"text":"xport as","turn":1,"type":"answer_delta"}
{"text":"ync func","turn":1,"type":"answer_delta"}
{"text":"tion fet","turn":1,"type":"answer_delta"}
{"text":"chWithRe","turn":1,"type":"answer_delta"}
{"text":"try(url:","turn":1,"type":"answer_delta"}
{"text":" string,","turn":1,"type":"answer_delta"}
{"text":" retries","turn":1,"type":"answer_delta"}
{"text":" = 3): P","turn":1,"type":"answer_delta"}
{"text":"romise<R","turn":1,"type":"answer_delta"}
{"text":"esponse>","turn":1,"type":"answer_delta"}
{"text":" {\n  for","turn":1,"type":"answer_delta"}
{"text":" (let at","turn":1,"type":"answer_delta"}
{"text":"tempt = ","turn":1,"type":"answer_delta"}
{"text":"0; attem","turn":1,"type":"answer_delta"}
{"text":"pt <= re","turn":1,"type":"answer_delta"}
{"text":"tries; a","turn":1,"type":"answer_delta"}
{"text":"ttempt++","turn":1,"type":"answer_delta"}
{"text":") {\n    ","turn":1,"type":"answer_delta"}
{"text":"const re","turn":1,"type":"answer_delta"}
{"text":"sponse =","turn":1,"type":"answer_delta"}
{"text":" await f","turn":1,"type":"answer_delta"}
{"text":"etch(url","turn":1,"type":"answer_delta"}
{"text":", { sign","turn":1,"type":"answer_delta"}
{"text":"al: Abor","turn":1,"type":"answer_delta"}
{"text":"tSignal.","turn":1,"type":"answer_delta"}
{"text":"timeout(","turn":1,"type":"answer_delta"}
{"text":"10_000) ","turn":1,"type":"answer_delta"}
{"text":"});\n    ","turn":1,"type":"answer_delta"}
{"text":"if (resp","turn":1,"type":"answer_delta"}
{"text":"onse.ok)","turn":1,"type":"answer_delta"}
{"text":" return ","turn":1,"type":"answer_delta"}
{"text":"response","turn":1,"type":"answer_delta"}
{"text":";\n    co","turn":1,"type":"answer_delta"}
{"text":"nsole.wa","turn":1,"type":"answer_delta"}
{"text":"rn(`atte","turn":1,"type":"answer_delta"}
{"text":"mpt ${at","turn":1,"type":"answer_delta"}
{"text":"tempt} f","turn":1,"type":"answer_delta"}
{"text":"ailed: $","turn":1,"type":"answer_delta"}
{"text":"{respons","turn":1,"type":"answer_delta"}
{"text":"e.status","turn":1,"type":"answer_delta"}
{"text":"}`);\n  }","turn":1,"type":"answer_delta"}
{"text":"\n  throw","turn":1,"type":"answer_delta"}
{"text":" new Err","turn":1,"type":"answer_delta"}
{"text":"or('gave","turn":1,"type":"answer_delta"}
{"text":" up afte","turn":1,"type":"answer_delta"}
{"text":"r ' + re","turn":1,"type":"answer_delta"}
{"text":"tries + ","turn":1,"type":"answer_delta"}
{"text":"' retrie","turn":1,"type":"answer_delta"}
{"text":"s');\n}\n`","turn":1,"type":"answer_delta"}
{"text":"``\n\nThe ","turn":1,"type":"answer_delta"}
{"text":"worker's","turn":1,"type":"answer_delta"}
{"text":" backoff","turn":1,"type":"answer_delta"}
{"text":", in Rus","turn":1,"type":"answer_delta"}
{"text":"t:\n\n```r","turn":1,"type":"answer_delta"}
{"text":"ust\nuse ","turn":1,"type":"answer_delta"}
{"text":"std::tim","turn":1,"type":"answer_delta"}
{"text":"e::Durat","turn":1,"type":"answer_delta"}
{"text":"ion;\n\n/*","turn":1,"type":"answer_delta"}
{"text":" Doubles","turn":1,"type":"answer_delta"}
{"text":" the pau","turn":1,"type":"answer_delta"}
{"text":"se after","turn":1,"type":"answer_delta"}
{"text":" each fa","turn":1,"type":"answer_delta"}
{"text":"iled att","turn":1,"type":"answer_delta"}
{"text":"empt. */","turn":1,"type":"answer_delta"}
{"text":"\nfn back","turn":1,"type":"answer_delta"}
{"text":"off(atte","turn":1,"type":"answer_delta"}
{"text":"mpt: u32","turn":1,"type":"answer_delta"}
{"text":") -> Dur","turn":1,"type":"answer_delta"}
{"text":"ation {\n","turn":1,"type":"answer_delta"}
{"text":"    let ","turn":1,"type":"answer_delta"}
{"text":"base = 0","turn":1,"type":"answer_delta"}
{"text":"x10u64; ","turn":1,"type":"answer_delta"}
{"text":"// 16 ms","turn":1,"type":"answer_delta"}
{"text":"\n    Dur","turn":1,"type":"answer_delta"}
{"text":"ation::f","turn":1,"type":"answer_delta"}
{"text":"rom_mill","turn":1,"type":"answer_delta"}
{"text":"is(base ","turn":1,"type":"answer_delta"}
{"text":"* 2u64.p","turn":1,"type":"answer_delta"}
{"text":"ow(attem","turn":1,"type":"answer_delta"}
{"text":"pt))\n}\n\n","turn":1,"type":"answer_delta"}
{"text":"fn main(","turn":1,"type":"answer_delta"}
{"text":") {\n    ","turn":1,"type":"answer_delta"}
{"text":"for atte","turn":1,"type":"answer_delta"}
{"text":"mpt in 0","turn":1,"type":"answer_delta"}
{"text":"..3 {\n  ","turn":1,"type":"answer_delta"}
{"text":"      pr","turn":1,"type":"answer_delta"}
{"text":"intln!(\"","turn":1,"type":"answer_delta"}
{"text":"attempt ","turn":1,"type":"answer_delta"}
{"text":"{attempt","turn":1,"type":"answer_delta"}
{"text":"}: wait ","turn":1,"type":"answer_delta"}
{"text":"{:?}\", b","turn":1,"type":"answer_delta"}
{"text":"ackoff(a","turn":1,"type":"answer_delta"}
{"text":"ttempt))","turn":1,"type":"answer_delta"}
{"text":";\n    }\n","turn":1,"type":"answer_delta"}
{"text":"}\n```\n\nT","turn":1,"type":"answer_delta"}
{"text":"o check ","turn":1,"type":"answer_delta"}
{"text":"it by ha","turn":1,"type":"answer_delta"}
{"text":"nd from ","turn":1,"type":"answer_delta"}
{"text":"the shel","turn":1,"type":"answer_delta"}
{"text":"l:\n\n```b","turn":1,"type":"answer_delta"}
{"text":"ash\n# Th","turn":1,"type":"answer_delta"}
{"text":"ree trie","turn":1,"type":"answer_delta"}
{"text":"s, two s","turn":1,"type":"answer_delta"}
{"text":"econds a","turn":1,"type":"answer_delta"}
{"text":"part.\nre","turn":1,"type":"answer_delta"}
{"text":"try() {\n","turn":1,"type":"answer_delta"}
{"text":"  for i ","turn":1,"type":"answer_delta"}
{"text":"in 1 2 3","turn":1,"type":"answer_delta"}
{"text":"; do\n   ","turn":1,"type":"answer_delta"}
{"text":" \"$@\" &&","turn":1,"type":"answer_delta"}
{"text":" return ","turn":1,"type":"answer_delta"}
{"text":"0\n    ec","turn":1,"type":"answer_delta"}
{"text":"ho \"atte","turn":1,"type":"answer_delta"}
{"text":"mpt $i f","turn":1,"type":"answer_delta"}
{"text":"ailed\" >","turn":1,"type":"answer_delta"}
{"text":"&2\n    s","turn":1,"type":"answer_delta"}
{"text":"leep 2\n ","turn":1,"type":"answer_delta"}
{"text":" done\n  ","turn":1,"type":"answer_delta"}
{"text":"return 1","turn":1,"type":"answer_delta"}
{"text":"\n}\n\nretr","turn":1,"type":"answer_delta"}
{"text":"y curl -","turn":1,"type":"answer_delta"}
{"text":"-max-tim","turn":1,"type":"answer_delta"}
{"text":"e 10 'ht","turn":1,"type":"answer_delta"}
{"text":"tps://ex","turn":1,"type":"answer_delta"}
{"text":"ample.co","turn":1,"type":"answer_delta"}
{"text":"m/slow'\n","turn":1,"type":"answer_delta"}
{"text":"```\n\nThe","turn":1,"type":"answer_delta"}
{"text":" setting","turn":1,"type":"answer_delta"}
{"text":"s it rea","turn":1,"type":"answer_delta"}
{"text":"ds, in `","turn":1,"type":"answer_delta"}
{"text":"config.j","turn":1,"type":"answer_delta"}
{"text":"son`:\n\n`","turn":1,"type":"answer_delta"}
{"text":"``json\n{","turn":1,"type":"answer_delta"}
{"text":"\n  \"retr","turn":1,"type":"answer_delta"}
{"text":"ies\": 3,","turn":1,"type":"answer_delta"}
{"text":"\n  \"time","turn":1,"type":"answer_delta"}
{"text":"out\": 10","turn":1,"type":"answer_delta"}
{"text":".5,\n  \"b","turn":1,"type":"answer_delta"}
{"text":"ackoff_m","turn":1,"type":"answer_delta"}
{"text":"s\": [250","turn":1,"type":"answer_delta"}
{"text":", 500, 1","turn":1,"type":"answer_delta"}
{"text":"000],\n  ","turn":1,"type":"answer_delta"}
{"text":"\"hosts\":","turn":1,"type":"answer_delta"}
{"text":" [\"examp","turn":1,"type":"answer_delta"}
{"text":"le.com\"]","turn":1,"type":"answer_delta"}
{"text":",\n  \"pro","turn":1,"type":"answer_delta"}
{"text":"xy\": nul","turn":1,"type":"answer_delta"}
{"text":"l,\n  \"ve","turn":1,"type":"answer_delta"}
{"text":"rbose\": ","turn":1,"type":"answer_delta"}
{"text":"false\n}\n","turn":1,"type":"answer_delta"}
{"text":"```\n\nThe","turn":1,"type":"answer_delta"}
{"text":" change ","turn":1,"type":"answer_delta"}
{"text":"to `runn","turn":1,"type":"answer_delta"}
{"text":"er.py`, ","turn":1,"type":"answer_delta"}
{"text":"as a pat","turn":1,"type":"answer_delta"}
{"text":"ch:\n\n```","turn":1,"type":"answer_delta"}
{"text":"diff\n---","turn":1,"type":"answer_delta"}
{"text":" a/src/i","turn":1,"type":"answer_delta"}
{"text":"ngest/ru","turn":1,"type":"answer_delta"}
{"text":"nner.py\n","turn":1,"type":"answer_delta"}
{"text":"+++ b/sr","turn":1,"type":"answer_delta"}
{"text":"c/ingest","turn":1,"type":"answer_delta"}
{"text":"/runner.","turn":1,"type":"answer_delta"}
{"text":"py\n@@ -4","turn":1,"type":"answer_delta"}
{"text":"1,3 +41,","turn":1,"type":"answer_delta"}
{"text":"6 @@ cla","turn":1,"type":"answer_delta"}
{"text":"ss Runne","turn":1,"type":"answer_delta"}
{"text":"r:\n     ","turn":1,"type":"answer_delta"}
{"text":"async de","turn":1,"type":"answer_delta"}
{"text":"f _fetch","turn":1,"type":"answer_delta"}
{"text":"(self, u","turn":1,"type":"answer_delta"}
{"text":"rl: str)","turn":1,"type":"answer_delta"}
{"text":" -> byte","turn":1,"type":"answer_delta"}
{"text":"s | None","turn":1,"type":"answer_delta"}
{"text":":\n-     ","turn":1,"type":"answer_delta"}
{"text":"   async","turn":1,"type":"answer_delta"}
{"text":" with as","turn":1,"type":"answer_delta"}
{"text":"yncio.ti","turn":1,"type":"answer_delta"}
{"text":"meout(10","turn":1,"type":"answer_delta"}
{"text":"):\n-    ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"return a","turn":1,"type":"answer_delta"}
{"text":"wait sel","turn":1,"type":"answer_delta"}
{"text":"f.client","turn":1,"type":"answer_delta"}
{"text":".get(url","turn":1,"type":"answer_delta"}
{"text":")\n+     ","turn":1,"type":"answer_delta"}
{"text":"   for a","turn":1,"type":"answer_delta"}
{"text":"ttempt i","turn":1,"type":"answer_delta"}
{"text":"n range(","turn":1,"type":"answer_delta"}
{"text":"self.ret","turn":1,"type":"answer_delta"}
{"text":"ries + 1","turn":1,"type":"answer_delta"}
{"text":"):\n+    ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"with con","turn":1,"type":"answer_delta"}
{"text":"textlib.","turn":1,"type":"answer_delta"}
{"text":"suppress","turn":1,"type":"answer_delta"}
{"text":"(Timeout","turn":1,"type":"answer_delta"}
{"text":"Error):\n","turn":1,"type":"answer_delta"}
{"text":"+       ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":" async w","turn":1,"type":"answer_delta"}
{"text":"ith asyn","turn":1,"type":"answer_delta"}
{"text":"cio.time","turn":1,"type":"answer_delta"}
{"text":"out(10.5","turn":1,"type":"answer_delta"}
{"text":"):\n+    ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"        ","turn":1,"type":"answer_delta"}
{"text":"return a","turn":1,"type":"answer_delta"}
{"text":"wait sel","turn":1,"type":"answer_delta"}
{"text":"f.client","turn":1,"type":"answer_delta"}
{"text":".get(url","turn":1,"type":"answer_delta"}
{"text":")\n+     ","turn":1,"type":"answer_delta"}
{"text":"   retur","turn":1,"type":"answer_delta"}
{"text":"n None\n`","turn":1,"type":"answer_delta"}
{"text":"``\n\nWhat","turn":1,"type":"answer_delta"}
{"text":" a slow ","turn":1,"type":"answer_delta"}
{"text":"host log","turn":1,"type":"answer_delta"}
{"text":"s:\n\n```\n","turn":1,"type":"answer_delta"}
{"text":"attempt ","turn":1,"type":"answer_delta"}
{"text":"0 timed ","turn":1,"type":"answer_delta"}
{"text":"out: htt","turn":1,"type":"answer_delta"}
{"text":"ps://exa","turn":1,"type":"answer_delta"}
{"text":"mple.com","turn":1,"type":"answer_delta"}
{"text":"/slow\nat","turn":1,"type":"answer_delta"}
{"text":"tempt 1 ","turn":1,"type":"answer_delta"}
{"text":"timed ou","turn":1,"type":"answer_delta"}
{"text":"t: https","turn":1,"type":"answer_delta"}
{"text":"://examp","turn":1,"type":"answer_delta"}
{"text":"le.com/s","turn":1,"type":"answer_delta"}
{"text":"low\n```\n","turn":1,"type":"answer_delta"}
{"text":"\nAnd the","turn":1,"type":"answer_delta"}
{"text":" same lo","turn":1,"type":"answer_delta"}
{"text":"op in El","turn":1,"type":"answer_delta"}
{"text":"ixir:\n\n`","turn":1,"type":"answer_delta"}
{"text":"``elixir","turn":1,"type":"answer_delta"}
{"text":"\nEnum.ea","turn":1,"type":"answer_delta"}
{"text":"ch(1..3,","turn":1,"type":"answer_delta"}
{"text":" fn i ->","turn":1,"type":"answer_delta"}
{"text":" IO.puts","turn":1,"type":"answer_delta"}
{"text":"(\"attemp","turn":1,"type":"answer_delta"}
{"text":"t #{i}\")","turn":1,"type":"answer_delta"}
{"text":" end)\n``","turn":1,"type":"answer_delta"}
{"text":"`","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"chatty","text":"## Retrying a slow host\n\nRetry the timeout where it happens, and keep `asyncio.timeout(10)` around one attempt. In the scraper:\n\n```python\nasync def fetch(self, url: str, retries: int = 3) -> bytes | None:\n    \"\"\"Fetch url, retrying a timeout.\"\"\"\n    for attempt in range(retries + 1):\n        try:\n            async with asyncio.timeout(10.5):\n                return await self.client.get(url)\n        except TimeoutError:\n            log.warning(\"attempt %d timed out: %s\", attempt, url)  # try again\n    return None\n```\n\nThe app's own helper, in Swift:\n\n```swift\n/// Asks again after a pause, up to `attempts` times.\nfunc retry<T>(_ attempts: Int = 3, _ body: () async throws -> T) async throws -> T {\n    for attempt in 1..<attempts {\n        do {\n            return try await body()\n        } catch {\n            print(\"attempt \\(attempt) failed: \\(error)\")\n            try await Task.sleep(for: .milliseconds(250 * attempt))\n        }\n    }\n    return try await body()\n}\n```\n\nThe browser client:\n\n```ts\n// Back off between attempts.\nexport async function fetchWithRetry(url: string, retries = 3): Promise<Response> {\n  for (let attempt = 0; attempt <= retries; attempt++) {\n    const response = await fetch(url, { signal: AbortSignal.timeout(10_000) });\n    if (response.ok) return response;\n    console.warn(`attempt ${attempt} failed: ${response.status}`);\n  }\n  throw new Error('gave up after ' + retries + ' retries');\n}\n```\n\nThe worker's backoff, in Rust:\n\n```rust\nuse std::time::Duration;\n\n/* Doubles the pause after each failed attempt. */\nfn backoff(attempt: u32) -> Duration {\n    let base = 0x10u64; // 16 ms\n    Duration::from_millis(base * 2u64.pow(attempt))\n}\n\nfn main() {\n    for attempt in 0..3 {\n        println!(\"attempt {attempt}: wait {:?}\", backoff(attempt));\n    }\n}\n```\n\nTo check it by hand from the shell:\n\n```bash\n# Three tries, two seconds apart.\nretry() {\n  for i in 1 2 3; do\n    \"$@\" && return 0\n    echo \"attempt $i failed\" >&2\n    sleep 2\n  done\n  return 1\n}\n\nretry curl --max-time 10 'https://example.com/slow'\n```\n\nThe settings it reads, in `config.json`:\n\n```json\n{\n  \"retries\": 3,\n  \"timeout\": 10.5,\n  \"backoff_ms\": [250, 500, 1000],\n  \"hosts\": [\"example.com\"],\n  \"proxy\": null,\n  \"verbose\": false\n}\n```\n\nThe change to `runner.py`, as a patch:\n\n```diff\n--- a/src/ingest/runner.py\n+++ b/src/ingest/runner.py\n@@ -41,3 +41,6 @@ class Runner:\n     async def _fetch(self, url: str) -> bytes | None:\n-        async with asyncio.timeout(10):\n-            return await self.client.get(url)\n+        for attempt in range(self.retries + 1):\n+            with contextlib.suppress(TimeoutError):\n+                async with asyncio.timeout(10.5):\n+                    return await self.client.get(url)\n+        return None\n```\n\nWhat a slow host logs:\n\n```\nattempt 0 timed out: https://example.com/slow\nattempt 1 timed out: https://example.com/slow\n```\n\nAnd the same loop in Elixir:\n\n```elixir\nEnum.each(1..3, fn i -> IO.puts(\"attempt #{i}\") end)\n```","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/edges-answer.jsonl
    static let edgesAnswer = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-023650-11d5","model":"chatty","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"## Sizin","turn":1,"type":"answer_delta"}
{"text":"g the ba","turn":1,"type":"answer_delta"}
{"text":"tch\n\nSta","turn":1,"type":"answer_delta"}
{"text":"rt from ","turn":1,"type":"answer_delta"}
{"text":"the arit","turn":1,"type":"answer_delta"}
{"text":"hmetic:\n","turn":1,"type":"answer_delta"}
{"text":"\n128 * 8","turn":1,"type":"answer_delta"}
{"text":" = 1024\n","turn":1,"type":"answer_delta"}
{"text":"\nSo one ","turn":1,"type":"answer_delta"}
{"text":"host tak","turn":1,"type":"answer_delta"}
{"text":"es 1024 ","turn":1,"type":"answer_delta"}
{"text":"rows, an","turn":1,"type":"answer_delta"}
{"text":"d 2 + 2 ","turn":1,"type":"answer_delta"}
{"text":"= 4 work","turn":1,"type":"answer_delta"}
{"text":"ers shar","turn":1,"type":"answer_delta"}
{"text":"e them. ","turn":1,"type":"answer_delta"}
{"text":"Per work","turn":1,"type":"answer_delta"}
{"text":"er:\n| Wo","turn":1,"type":"answer_delta"}
{"text":"rkers | ","turn":1,"type":"answer_delta"}
{"text":"Rows eac","turn":1,"type":"answer_delta"}
{"text":"h | Tota","turn":1,"type":"answer_delta"}
{"text":"l |\n|---","turn":1,"type":"answer_delta"}
{"text":"|--:|--:","turn":1,"type":"answer_delta"}
{"text":"|\n| 2 | ","turn":1,"type":"answer_delta"}
{"text":"512 | 10","turn":1,"type":"answer_delta"}
{"text":"24 |\n| 4","turn":1,"type":"answer_delta"}
{"text":" | 256 |","turn":1,"type":"answer_delta"}
{"text":" 1024 |\n","turn":1,"type":"answer_delta"}
{"text":"\nThe loa","turn":1,"type":"answer_delta"}
{"text":"der writ","turn":1,"type":"answer_delta"}
{"text":"es its s","turn":1,"type":"answer_delta"}
{"text":"ettings ","turn":1,"type":"answer_delta"}
{"text":"with a h","turn":1,"type":"answer_delta"}
{"text":"ere-docu","turn":1,"type":"answer_delta"}
{"text":"ment, an","turn":1,"type":"answer_delta"}
{"text":"d sizes ","turn":1,"type":"answer_delta"}
{"text":"its mask","turn":1,"type":"answer_delta"}
{"text":" with sh","turn":1,"type":"answer_delta"}
{"text":"ifts:\n\n`","turn":1,"type":"answer_delta"}
{"text":"``bash\nc","turn":1,"type":"answer_delta"}
{"text":"at <<EOF","turn":1,"type":"answer_delta"}
{"text":" > batch","turn":1,"type":"answer_delta"}
{"text":".conf\nro","turn":1,"type":"answer_delta"}
{"text":"ws=1024\n","turn":1,"type":"answer_delta"}
{"text":"EOF\necho","turn":1,"type":"answer_delta"}
{"text":" $((a<<b","turn":1,"type":"answer_delta"}
{"text":"))\n(( ma","turn":1,"type":"answer_delta"}
{"text":"sk = 1 <","turn":1,"type":"answer_delta"}
{"text":"<bits ))","turn":1,"type":"answer_delta"}
{"text":"\necho \"d","turn":1,"type":"answer_delta"}
{"text":"one\"\n```","turn":1,"type":"answer_delta"}
{"text":"\n\nIn Rub","turn":1,"type":"answer_delta"}
{"text":"y, `<<` ","turn":1,"type":"answer_delta"}
{"text":"appends;","turn":1,"type":"answer_delta"}
{"text":" a here-","turn":1,"type":"answer_delta"}
{"text":"document","turn":1,"type":"answer_delta"}
{"text":"'s marke","turn":1,"type":"answer_delta"}
{"text":"r follow","turn":1,"type":"answer_delta"}
{"text":"s a spac","turn":1,"type":"answer_delta"}
{"text":"e:\n\n```r","turn":1,"type":"answer_delta"}
{"text":"uby\nlist","turn":1,"type":"answer_delta"}
{"text":"<<Const\n","turn":1,"type":"answer_delta"}
{"text":"total = ","turn":1,"type":"answer_delta"}
{"text":"1 << 2\ns","turn":1,"type":"answer_delta"}
{"text":"ql = <<~","turn":1,"type":"answer_delta"}
{"text":"SQL\n  SE","turn":1,"type":"answer_delta"}
{"text":"LECT cou","turn":1,"type":"answer_delta"}
{"text":"nt(*) FR","turn":1,"type":"answer_delta"}
{"text":"OM rows\n","turn":1,"type":"answer_delta"}
{"text":"SQL\nputs","turn":1,"type":"answer_delta"}
{"text":" sql\n```","turn":1,"type":"answer_delta"}
{"text":"\n\nSee [t","turn":1,"type":"answer_delta"}
{"text":"he sizin","turn":1,"type":"answer_delta"}
{"text":"g guide]","turn":1,"type":"answer_delta"}
{"text":"[1] and ","turn":1,"type":"answer_delta"}
{"text":"[the loa","turn":1,"type":"answer_delta"}
{"text":"der's no","turn":1,"type":"answer_delta"}
{"text":"tes][2].","turn":1,"type":"answer_delta"}
{"text":"\n\n[1]: h","turn":1,"type":"answer_delta"}
{"text":"ttps://a","turn":1,"type":"answer_delta"}
{"text":".example","turn":1,"type":"answer_delta"}
{"text":"/docs\n[2","turn":1,"type":"answer_delta"}
{"text":"]: https","turn":1,"type":"answer_delta"}
{"text":"://b.exa","turn":1,"type":"answer_delta"}
{"text":"mple/gui","turn":1,"type":"answer_delta"}
{"text":"de \"Load","turn":1,"type":"answer_delta"}
{"text":"er notes","turn":1,"type":"answer_delta"}
{"text":"\"\n\n|x| i","turn":1,"type":"answer_delta"}
{"text":"s the ab","turn":1,"type":"answer_delta"}
{"text":"solute v","turn":1,"type":"answer_delta"}
{"text":"alue of ","turn":1,"type":"answer_delta"}
{"text":"x, so |-","turn":1,"type":"answer_delta"}
{"text":"3| = 3.\n","turn":1,"type":"answer_delta"}
{"text":"|rows - ","turn":1,"type":"answer_delta"}
{"text":"1024| is","turn":1,"type":"answer_delta"}
{"text":" how far","turn":1,"type":"answer_delta"}
{"text":" a host ","turn":1,"type":"answer_delta"}
{"text":"is from ","turn":1,"type":"answer_delta"}
{"text":"full.\n\nT","turn":1,"type":"answer_delta"}
{"text":"he answe","turn":1,"type":"answer_delta"}
{"text":"r, then:","turn":1,"type":"answer_delta"}
{"text":"\n\n42","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"chatty","text":"## Sizing the batch\n\nStart from the arithmetic:\n\n128 * 8 = 1024\n\nSo one host takes 1024 rows, and 2 + 2 = 4 workers share them. Per worker:\n| Workers | Rows each | Total |\n|---|--:|--:|\n| 2 | 512 | 1024 |\n| 4 | 256 | 1024 |\n\nThe loader writes its settings with a here-document, and sizes its mask with shifts:\n\n```bash\ncat <<EOF > batch.conf\nrows=1024\nEOF\necho $((a<<b))\n(( mask = 1 <<bits ))\necho \"done\"\n```\n\nIn Ruby, `<<` appends; a here-document's marker follows a space:\n\n```ruby\nlist<<Const\ntotal = 1 << 2\nsql = <<~SQL\n  SELECT count(*) FROM rows\nSQL\nputs sql\n```\n\nSee [the sizing guide][1] and [the loader's notes][2].\n\n[1]: https://a.example/docs\n[2]: https://b.example/guide \"Loader notes\"\n\n|x| is the absolute value of x, so |-3| = 3.\n|rows - 1024| is how far a host is from full.\n\nThe answer, then:\n\n42","turn":1,"type":"result"}
"""#
    /// tests/business/media/Fixtures/install-video-done.txt
    static let installVideoDone = #"""
environment already exists at /Users/someone/.apogee/training/venv
installing the 'video' set: torch>=2.6 diffusers>=0.35 transformers>=4.46 accelerate>=1.0 ftfy>=6.1 sentencepiece>=0.2 peft>=0.13 ...
installed

python env: at /Users/someone/.apogee/training/venv; sets: video
"""#
    /// tests/business/media/Fixtures/install-video-failed.err
    static let installVideoFailedErr = #"""
apogee train: pip install for the 'video' set failed (exit code 1):
ERROR: Could not install packages: this sandbox's pip is a stub with no network
"""#
    /// tests/business/media/Fixtures/generate-refused-runtime.err
    static let generateRefusedErr = #"""
apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video
"""#
}
#endif
