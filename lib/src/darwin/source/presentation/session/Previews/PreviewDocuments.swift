#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum SessionDocuments {
    /// tests/data/protocol/Fixtures/mock-actor.jsonl
    static let mockActor = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel"],"ask":true,"events":["session","thinking","thinking_delta","memory","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished"],"schema":"2026-10-13","tools":true},"model":"actor","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"text":"They wan","turn":1,"type":"thinking_delta"}
{"text":"t a note","turn":1,"type":"thinking_delta"}
{"text":" written","turn":1,"type":"thinking_delta"}
{"text":". I shou","turn":1,"type":"thinking_delta"}
{"text":"ld ask w","turn":1,"type":"thinking_delta"}
{"text":"hat to c","turn":1,"type":"thinking_delta"}
{"text":"all it.","turn":1,"type":"thinking_delta"}
{"questions":[{"header":"Name","multi_select":false,"options":[{"description":"A to-do list","label":"todo.md"},{"description":"Loose ideas","label":"ideas.md"}],"question":"What should the note be called?"}],"turn":1,"type":"question"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"thinking"}
{"kind":"permission","questions":[{"header":"Permission","multi_select":false,"options":[{"description":"Allow this once","label":"yes"},{"description":"Deny","label":"no"},{"description":"Allow, and remember it in the config","label":"always"},{"description":"Allow for the rest of this session","label":"session"}],"question":"Allow write_file on todo.md?"}],"target":"todo.md","tool":"write_file","turn":1,"type":"question"}
{"text":"[tool] write_file","turn":1,"type":"tool_status"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"I wrote ","turn":1,"type":"answer_delta"}
{"text":"todo.md ","turn":1,"type":"answer_delta"}
{"text":"with one","turn":1,"type":"answer_delta"}
{"text":" item: b","turn":1,"type":"answer_delta"}
{"text":"uy milk.","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"actor","text":"I wrote todo.md with one item: buy milk.","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/execute-play.jsonl
    static let executePlay = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"root","next_turn":1,"protocol_version":1,"type":"session"}
{"text":"stage 1/2 first — asking utility (helper): One: hello","turn":1,"type":"tool_status"}
{"text":"stage 2/2 second — asking chat (root): Two: HELPER<One: hello>","turn":1,"type":"tool_status"}
{"turn":1,"type":"answer_start"}
{"text":"ROOT<Two: HELPER<One: hello>>","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"root","text":"ROOT<Two: HELPER<One: hello>>","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/execute-orchestrate.jsonl
    static let executeOrchestrate = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"root","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"text":"[tool] play_echo2","turn":1,"type":"tool_status"}
{"text":"play — the model chose echo2: 2 member calls, utility → chat","turn":1,"type":"tool_status"}
{"text":"stage 1/2 first — asking utility (helper): One: hello","turn":1,"type":"tool_status"}
{"text":"stage 2/2 second — asking chat (root): Two: HELPER<One: hello>","turn":1,"type":"tool_status"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"ANSWER<e","turn":1,"type":"answer_delta"}
{"text":"cho2 ans","turn":1,"type":"answer_delta"}
{"text":"wered:\nR","turn":1,"type":"answer_delta"}
{"text":"OOT<Two:","turn":1,"type":"answer_delta"}
{"text":" HELPER<","turn":1,"type":"answer_delta"}
{"text":"One: hel","turn":1,"type":"answer_delta"}
{"text":"lo>>>","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"root","text":"ANSWER<echo2 answered:\nROOT<Two: HELPER<One: hello>>>","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/execute-play-refused.jsonl
    static let executePlayRefused = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"root","next_turn":1,"protocol_version":1,"type":"session"}
{"message":"no symphony named 'nosuch' (there are: critique-revise, describe-answer, digest, extract-actions, extract-facts, redact-text, summarize-verify, echo2)","turn":1,"type":"error"}
"""#
    /// tests/data/protocol/Fixtures/execute-project.jsonl
    static let executeProject = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-20","tools":true},"model":"root","next_turn":1,"protocol_version":1,"type":"session"}
{"text":"persona: coder, the shipped coding prompt, in every request -- -s replaces it","type":"notice"}
{"text":"project context: AGENTS.md, 11 words, in every request","type":"notice"}
{"text":"attaching . (2 files, 89 bytes)","type":"notice"}
{"text":"attached .: 2 files, 2 chunks, no map: the model's window is unknown, searched by its words: no embedding model is set ('apogee config set-default-embedding') -- its excerpts are retrieved each turn","turn":1,"type":"notice"}
{"text":"graph: 2 nodes, 1 edge (supported: c 1; skipped: .md 1)","turn":1,"type":"notice"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"ROOT<hi>","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"root","text":"ROOT<hi>","turn":1,"type":"result"}
"""#
    /// tests/business/reads/Fixtures/symphonies-list.json
    static let symphoniesList = #"""
{
 "data": [
  {
   "description": "Critique a draft against what it is meant to do, then revise it to answer the critique.",
   "input": {
    "description": "The draft, opening with a line that says what it is for when that is not obvious.",
    "image": false
   },
   "name": "critique-revise",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/critique-revise.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "answer_tokens": 600,
     "image": false,
     "name": "critique",
     "prompt": "Below is a draft. Work out what it is meant to do -- from its first line when that says so, otherwise from the draft itself. Then list, most important first, at most five specific ways it falls short of that: something unclear, missing, wrong, out of order or longer than it needs to be. Quote the part each point is about. When it does its job, say so and list nothing.\n\nDraft:\n{{input}}\n",
     "role": "utility"
    },
    {
     "answer_tokens": 2000,
     "image": false,
     "name": "revise",
     "prompt": "Here is a draft and a critique of it.\n\nDraft:\n{{input}}\n\nCritique:\n{{critique}}\n\nRevise the draft so it answers every point in the critique, keeping its purpose, its facts and its voice. Change only what the critique calls for. Reply with the revised draft only.\n",
     "role": "chat"
    }
   ]
  },
  {
   "description": "Describe an image with the vision member, then answer a question about it from the description.",
   "input": {
    "description": "A question about the image, which is given with --image.",
    "image": true
   },
   "name": "describe-answer",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/describe-answer.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "answer_tokens": 500,
     "image": true,
     "name": "describe",
     "prompt": "Describe this image in detail: the objects and the people in it, any text it shows word for word, the colours, the counts and the layout. Pay particular attention to anything that bears on this question: {{input}}\n",
     "role": "vision"
    },
    {
     "answer_tokens": 300,
     "image": false,
     "name": "answer",
     "prompt": "Below is a description of an image, and a question about the image.\n\nDescription:\n{{describe}}\n\nQuestion: {{input}}\n\nAnswer the question from the description alone. If the description does not say, reply that the description of the image does not say.\n",
     "role": "chat"
    }
   ]
  },
  {
   "description": "Summarize and check a passage by playing summarize-verify, then card it with a title, tags and a one-line summary.",
   "input": {
    "description": "The passage to digest.",
    "image": false
   },
   "name": "digest",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/digest.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "image": false,
     "name": "summary",
     "play": "summarize-verify"
    },
    {
     "answer_tokens": 300,
     "image": false,
     "name": "card",
     "prompt": "Here is a summary of a passage. Give it a short title, three to five lower-case tags, and a one-line summary of at most twenty words, all drawn from the summary itself.\n\nSummary:\n{{summary}}\n",
     "role": "extraction",
     "schema": "{\n  \"type\": \"object\",\n  \"properties\": {\n    \"title\": {\"type\": \"string\"},\n    \"tags\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n    \"one_line\": {\"type\": \"string\"}\n  },\n  \"required\": [\"title\", \"tags\", \"one_line\"],\n  \"additionalProperties\": false\n}\n"
    }
   ]
  },
  {
   "description": "Pull the action items out of notes -- who does what, by when -- as one JSON document, held to its schema.",
   "input": {
    "description": "The notes to read.",
    "image": false
   },
   "name": "extract-actions",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/extract-actions.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "answer_tokens": 800,
     "image": false,
     "name": "extract",
     "prompt": "Read the notes below and list every action item: something a person agreed or was asked to do. For each, record who will do it, the action starting with a verb, and when it is due. Write \"unassigned\" when no one took it and \"not stated\" when no date was given -- never guess either. Leave the list empty when the notes hold no action items.\n\nNotes:\n{{input}}\n",
     "role": "extraction",
     "schema": "{\n  \"type\": \"object\",\n  \"properties\": {\n    \"actions\": {\n      \"type\": \"array\",\n      \"items\": {\n        \"type\": \"object\",\n        \"properties\": {\n          \"owner\": {\"type\": \"string\"},\n          \"action\": {\"type\": \"string\"},\n          \"due\": {\"type\": \"string\"}\n        },\n        \"required\": [\"owner\", \"action\", \"due\"],\n        \"additionalProperties\": false\n      }\n    }\n  },\n  \"required\": [\"actions\"],\n  \"additionalProperties\": false\n}\n"
    }
   ]
  },
  {
   "description": "Pull the key facts out of a text as one JSON document, held to its schema.",
   "input": {
    "description": "The text to read.",
    "image": false
   },
   "name": "extract-facts",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/extract-facts.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "answer_tokens": 800,
     "image": false,
     "name": "extract",
     "prompt": "Read the text below and record its key facts: what it is about, the people and the places it names, the dates it gives, and each fact it states, one per entry. Leave a list empty when the text names none.\n\nText:\n{{input}}\n",
     "role": "extraction",
     "schema": "{\n  \"type\": \"object\",\n  \"properties\": {\n    \"topic\": {\"type\": \"string\"},\n    \"people\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n    \"places\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n    \"dates\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n    \"facts\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}}\n  },\n  \"required\": [\"topic\", \"people\", \"places\", \"dates\", \"facts\"],\n  \"additionalProperties\": false\n}\n"
    }
   ]
  },
  {
   "description": "List the personal details and secrets in a text, then rewrite it with placeholders in their place.",
   "input": {
    "description": "The text to redact.",
    "image": false
   },
   "name": "redact-text",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/redact-text.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "answer_tokens": 800,
     "image": false,
     "name": "find",
     "prompt": "List every private detail in the text below, each exactly as it is written there, with its category:\nemail (an email address), phone (a phone number), person (a person's name), address (a postal or street address), credential (a password, API key, token or other secret), id (an account, card, passport or other identifying number).\nList each detail once. Leave the list empty when the text has none.\n\nText:\n{{input}}\n",
     "role": "utility",
     "schema": "{\n  \"type\": \"object\",\n  \"properties\": {\n    \"found\": {\n      \"type\": \"array\",\n      \"items\": {\n        \"type\": \"object\",\n        \"properties\": {\n          \"category\": {\"type\": \"string\", \"enum\": [\"email\", \"phone\", \"person\", \"address\", \"credential\", \"id\"]},\n          \"text\": {\"type\": \"string\"}\n        },\n        \"required\": [\"category\", \"text\"],\n        \"additionalProperties\": false\n      }\n    }\n  },\n  \"required\": [\"found\"],\n  \"additionalProperties\": false\n}\n"
    },
    {
     "answer_tokens": 1500,
     "image": false,
     "name": "redact",
     "prompt": "Here is a text and a list of the private details found in it.\n\nText:\n{{input}}\n\nPrivate details:\n{{find}}\n\nRewrite the text with each listed detail replaced by its category in capitals and brackets: [EMAIL], [PHONE], [PERSON], [ADDRESS], [CREDENTIAL] or [ID]. Replace any other detail of those kinds the list missed the same way. Change nothing else. Reply with the rewritten text only.\n",
     "role": "chat"
    }
   ]
  },
  {
   "description": "Summarize a passage, then check the summary against the passage and correct it.",
   "input": {
    "description": "The passage to summarize.",
    "image": false
   },
   "name": "summarize-verify",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/summarize-verify.yaml",
   "problems": [],
   "source": "shipped",
   "stages": [
    {
     "answer_tokens": 400,
     "image": false,
     "name": "summarize",
     "prompt": "Summarize the passage below in at most three sentences. Keep every name, number and date exactly as the passage gives it.\n\nPassage:\n{{input}}\n",
     "role": "utility"
    },
    {
     "answer_tokens": 400,
     "image": false,
     "name": "verify",
     "prompt": "Here is a passage and a summary of it.\n\nPassage:\n{{input}}\n\nSummary:\n{{summarize}}\n\nCheck the summary against the passage. Correct anything it gets wrong and add any key fact it leaves out, in at most three sentences. Reply with the final summary only.\n",
     "role": "chat"
    }
   ]
  },
  {
   "description": "A one-stage check.",
   "input": {
    "description": "",
    "image": false
   },
   "name": "quick-check",
   "object": "symphony",
   "overrides": false,
   "problems": [],
   "source": "config",
   "stages": [
    {
     "image": false,
     "name": "check",
     "prompt": "Check this for mistakes: {{input}}",
     "role": "chat"
    }
   ]
  },
  {
   "description": "Summarize then check.",
   "input": {
    "description": "The text to summarize.",
    "image": false
   },
   "name": "notes-digest",
   "object": "symphony",
   "overrides": false,
   "path": "/Users/someone/.apogee/symphonies/notes-digest.yaml",
   "problems": [],
   "source": "file",
   "stages": [
    {
     "image": false,
     "name": "summarize",
     "prompt": "Summarize: {{input}}",
     "role": "utility"
    },
    {
     "image": false,
     "name": "verify",
     "prompt": "Check this summary against the text. Summary: {{summarize}} Text: {{input}}",
     "role": "chat"
    }
   ]
  }
 ],
 "object": "list",
 "problems": []
}
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
    /// tests/business/reads/Fixtures/chats-show.json
    static let chatsShow = #"""
{"attachments":[{"bytes":71,"files":1,"name":"/Users/someone/Documents/report.md"}],"backend":"chatty","id":"20261022-182803-f9f6","model":"","name":"Planning A Note","object":"chat","title":"Planning A Note","turns":[{"answer":{"text":"Then write the date first, as 2026-10-22.","tools":[]},"answered":true,"compacted":{"summary":"They asked how to write a note; a short list, the date first."},"turn":2,"user":{"attachments":[],"text":""}},{"answer":{"text":"The dev server listens on port 8080.","tools":[]},"answered":true,"turn":3,"user":{"attachments":[],"text":"Which port does the dev server use?"}},{"answer":{"text":"notes.md holds two items: **buy milk** and *call the plumber*.","tools":[{"name":"read_file","result_bytes":30,"summary":"{\"path\":\"Documents/notes.md\"}"}]},"answered":true,"turn":4,"user":{"attachments":[],"text":"What is in notes.md?"}},{"answer":{"text":"","tools":[]},"answered":false,"turn":4,"user":{"attachments":[],"text":"Is the launch still on?"}},{"answer":{"text":"The report says the launch moved to **November**:\n\n- the beta ends on 2026-10-31\n- the launch is on 2026-11-12","tools":[]},"answered":true,"turn":5,"user":{"attachments":[{"bytes":71,"files":1,"name":"/Users/someone/Documents/report.md"}],"text":"What does the report I attached say?"}}]}
"""#
    /// tests/business/reads/Fixtures/chats-show-refused.err
    static let chatsShowRefused = #"""
A subcommand is required
Run with --help for more information.
"""#
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
    /// tests/business/reads/Fixtures/chats-show-markdown.json
    static let chatsShowMarkdown = #"""
{"attachments":[],"backend":"chatty","id":"20261022-193608-43af","model":"","name":"Timeouts That Keep The Batch","object":"chat","title":"Timeouts That Keep The Batch","turns":[{"answer":{"text":"## Timeouts that keep the batch going\n\nCatch the timeout **inside** the child, not around the group. The `TaskGroup` stays *all-or-nothing* for real failures, and a slow host just logs and moves on:\n\n```python\nasync def _fetch(self, url: str) -> bytes | None:\n    try:\n        async with asyncio.timeout(10):\n            return await self.client.get(url)\n    except TimeoutError:\n        log.warning(\"timed out: %s\", url)  # the batch keeps going\n        return None\n```\n\nWhat changes:\n\n- `_fetch()` returns `None` on a timeout\n  - the caller skips it\n  - the log keeps the address\n- every other failure still cancels the group\n\n1. Run the scraper again.\n2. Check the log for `timed out`.\n\n> A cancel from outside still arrives as `CancelledError`, so shutdown works.\n\n| Option | Keeps the batch | Cancels on failure |\n|---|:-:|---|\n| `asyncio.timeout` in the child | yes | yes |\n| `gather(..., return_exceptions=True)` | yes | no |\n\n---\n\nSee the [asyncio docs](https://docs.python.org/3/library/asyncio-task.html) or [write to the team](mailto:team@example.com); the notes are in [runner.md](file:///Users/someone/Documents/runner.md).\n\n![The batch timeline](https://example.com/timeline.png)","tools":[]},"answered":true,"turn":1,"user":{"attachments":[],"text":"My scraper's TaskGroup dies when one host times out. How do I keep the batch going?"}}]}
"""#
}
#endif
