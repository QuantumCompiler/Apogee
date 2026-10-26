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
    /// tests/business/reads/Fixtures/chats-show-code.json
    static let chatsShowCode = #"""
{"attachments":[],"backend":"chatty","id":"20261022-203212-b200","model":"","name":"Retrying A Slow Host","object":"chat","title":"Retrying A Slow Host","turns":[{"answer":{"text":"## Retrying a slow host\n\nRetry the timeout where it happens, and keep `asyncio.timeout(10)` around one attempt. In the scraper:\n\n```python\nasync def fetch(self, url: str, retries: int = 3) -> bytes | None:\n    \"\"\"Fetch url, retrying a timeout.\"\"\"\n    for attempt in range(retries + 1):\n        try:\n            async with asyncio.timeout(10.5):\n                return await self.client.get(url)\n        except TimeoutError:\n            log.warning(\"attempt %d timed out: %s\", attempt, url)  # try again\n    return None\n```\n\nThe app's own helper, in Swift:\n\n```swift\n/// Asks again after a pause, up to `attempts` times.\nfunc retry<T>(_ attempts: Int = 3, _ body: () async throws -> T) async throws -> T {\n    for attempt in 1..<attempts {\n        do {\n            return try await body()\n        } catch {\n            print(\"attempt \\(attempt) failed: \\(error)\")\n            try await Task.sleep(for: .milliseconds(250 * attempt))\n        }\n    }\n    return try await body()\n}\n```\n\nThe browser client:\n\n```ts\n// Back off between attempts.\nexport async function fetchWithRetry(url: string, retries = 3): Promise<Response> {\n  for (let attempt = 0; attempt <= retries; attempt++) {\n    const response = await fetch(url, { signal: AbortSignal.timeout(10_000) });\n    if (response.ok) return response;\n    console.warn(`attempt ${attempt} failed: ${response.status}`);\n  }\n  throw new Error('gave up after ' + retries + ' retries');\n}\n```\n\nThe worker's backoff, in Rust:\n\n```rust\nuse std::time::Duration;\n\n/* Doubles the pause after each failed attempt. */\nfn backoff(attempt: u32) -> Duration {\n    let base = 0x10u64; // 16 ms\n    Duration::from_millis(base * 2u64.pow(attempt))\n}\n\nfn main() {\n    for attempt in 0..3 {\n        println!(\"attempt {attempt}: wait {:?}\", backoff(attempt));\n    }\n}\n```\n\nTo check it by hand from the shell:\n\n```bash\n# Three tries, two seconds apart.\nretry() {\n  for i in 1 2 3; do\n    \"$@\" && return 0\n    echo \"attempt $i failed\" >&2\n    sleep 2\n  done\n  return 1\n}\n\nretry curl --max-time 10 'https://example.com/slow'\n```\n\nThe settings it reads, in `config.json`:\n\n```json\n{\n  \"retries\": 3,\n  \"timeout\": 10.5,\n  \"backoff_ms\": [250, 500, 1000],\n  \"hosts\": [\"example.com\"],\n  \"proxy\": null,\n  \"verbose\": false\n}\n```\n\nThe change to `runner.py`, as a patch:\n\n```diff\n--- a/src/ingest/runner.py\n+++ b/src/ingest/runner.py\n@@ -41,3 +41,6 @@ class Runner:\n     async def _fetch(self, url: str) -> bytes | None:\n-        async with asyncio.timeout(10):\n-            return await self.client.get(url)\n+        for attempt in range(self.retries + 1):\n+            with contextlib.suppress(TimeoutError):\n+                async with asyncio.timeout(10.5):\n+                    return await self.client.get(url)\n+        return None\n```\n\nWhat a slow host logs:\n\n```\nattempt 0 timed out: https://example.com/slow\nattempt 1 timed out: https://example.com/slow\n```\n\nAnd the same loop in Elixir:\n\n```elixir\nEnum.each(1..3, fn i -> IO.puts(\"attempt #{i}\") end)\n```","tools":[]},"answered":true,"turn":1,"user":{"attachments":[],"text":"Show me the retry everywhere it runs: the scraper, the app, the browser, the worker, the shell, the settings and the patch."}}]}
"""#
    /// tests/data/protocol/Fixtures/set-between-turns.jsonl
    static let setBetweenTurns = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-005338-3a74","model":"asker","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"questions":[{"header":"Name","multi_select":false,"options":[{"description":"A to-do list","label":"todo.md"},{"description":"Loose ideas","label":"ideas.md"}],"question":"What should the note be called?"}],"turn":1,"type":"question"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"I'll cal","turn":1,"type":"answer_delta"}
{"text":"l it tod","turn":1,"type":"answer_delta"}
{"text":"o.md.","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"asker","text":"I'll call it todo.md.","turn":1,"type":"result"}
{"text":"temperature: 0.9 -- from the next turn","type":"notice"}
"""#
    /// tests/data/protocol/Fixtures/question-cancelled.jsonl
    static let questionCancelled = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-005338-158d","model":"asker","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"questions":[{"header":"Name","multi_select":false,"options":[{"description":"A to-do list","label":"todo.md"},{"description":"Loose ideas","label":"ideas.md"}],"question":"What should the note be called?"}],"turn":1,"type":"question"}
{"finish_reason":"cancelled","model":"asker","text":"","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/permission-cancelled.jsonl
    static let permissionCancelled = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-005339-ea48","model":"actor","next_turn":1,"protocol_version":1,"type":"session"}
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
{"detail":"--- /dev/null\n+++ b/todo.md\n@@ -0,0 +1 @@\n+- buy milk\n","kind":"permission","questions":[{"header":"Permission","multi_select":false,"options":[{"description":"Allow this once","label":"yes"},{"description":"Deny","label":"no"},{"description":"Allow, and remember it in the config","label":"always"},{"description":"Allow for the rest of this session","label":"session"}],"question":"Allow write_file on todo.md?"}],"target":"todo.md","tool":"write_file","turn":1,"type":"question"}
{"finish_reason":"cancelled","model":"actor","text":"","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/resumed-detach.jsonl
    static let resumedDetach = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-005344-e8cc","model":"plain","next_turn":2,"protocol_version":1,"type":"session"}
{"text":"detached Documents/report.md","type":"notice"}
"""#
    /// tests/business/reads/Fixtures/chats-show-attached.json
    static let chatsShowAttached = #"""
{"attachments":[{"bytes":30,"files":1,"name":"Documents/report.md"}],"backend":"plain","id":"20261026-005344-e8cc","model":"","name":"Dev Server Port","object":"chat","title":"Dev Server Port","turns":[{"answer":{"text":"The dev server listens on port 8080.","tools":[]},"answered":true,"turn":1,"user":{"attachments":[],"text":"What does the report I attached say?"}}],"warnings":[]}
"""#
    /// tests/data/protocol/Fixtures/resumed-warnings.jsonl
    static let resumedWarnings = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-005342-bd06","model":"plain","next_turn":3,"protocol_version":1,"type":"session"}
{"text":"resume: a message could not be read and was skipped","type":"notice"}
"""#
    /// tests/business/reads/Fixtures/chats-show-warnings.json
    static let chatsShowWarnings = #"""
{"attachments":[],"backend":"plain","id":"20261026-005342-bd06","model":"","name":"Dev Server Port","object":"chat","title":"Dev Server Port","turns":[{"answer":{"text":"The dev server listens on port 8080.\n\nNothing else: the launch is on 2026-11-12.","tools":[]},"answered":true,"turn":2,"user":{"attachments":[],"text":"Which port does the dev server use?"}}],"warnings":[{"kind":"field_dropped","message":"a message could not be read and was skipped"}]}
"""#
    /// tests/data/protocol/Fixtures/resumed-warnings-older.jsonl
    static let resumedWarningsOlder = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","document"],"schema":"2026-10-24","tools":true},"chat_id":"20261026-005343-fff6","model":"plain","next_turn":3,"protocol_version":1,"type":"session"}
"""#
    /// tests/business/reads/Fixtures/chats-show-warnings-older.json
    static let chatsShowWarningsOlder = #"""
{"attachments":[],"backend":"plain","id":"20261026-005343-fff6","model":"","name":"Dev Server Port","object":"chat","title":"Dev Server Port","turns":[{"answer":{"text":"The dev server listens on port 8080.\n\nNothing else: the launch is on 2026-11-12.","tools":[]},"answered":true,"turn":2,"user":{"attachments":[],"text":"Which port does the dev server use?"}}]}
"""#
    /// tests/business/reads/Fixtures/chats-show-warnings-older.err
    static let chatsShowWarningsOlderErr = #"""
apogee chats: a message could not be read and was skipped
"""#
    /// tests/data/protocol/Fixtures/chat-under-suite.jsonl
    static let chatUnderSuite = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-065701-9916","model":"plain","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"PLAIN-SA","turn":1,"type":"answer_delta"}
{"text":"YS hello","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"plain","text":"PLAIN-SAYS hello","turn":1,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/resumed-under-suite.jsonl
    static let resumedUnderSuite = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-065701-9916","model":"fastroot","next_turn":2,"protocol_version":1,"type":"session"}
{"text":"utility — restating the follow-up to rank the tools, with fasthelper","turn":2,"type":"tool_status"}
{"turn":2,"type":"thinking"}
{"turn":2,"type":"answer_start"}
{"text":"FASTROOT","turn":2,"type":"answer_delta"}
{"text":"-SAYS he","turn":2,"type":"answer_delta"}
{"text":"llo","turn":2,"type":"answer_delta"}
{"turn":2,"type":"answer_end"}
{"finish_reason":"stop","model":"fastroot","text":"FASTROOT-SAYS hello","turn":2,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/resumed-suite-off.jsonl
    static let resumedSuiteOff = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-065701-9916","model":"fastroot","next_turn":3,"protocol_version":1,"type":"session"}
{"turn":3,"type":"thinking"}
{"turn":3,"type":"answer_start"}
{"text":"FASTROOT","turn":3,"type":"answer_delta"}
{"text":"-SAYS he","turn":3,"type":"answer_delta"}
{"text":"llo","turn":3,"type":"answer_delta"}
{"turn":3,"type":"answer_end"}
{"finish_reason":"stop","model":"fastroot","text":"FASTROOT-SAYS hello","turn":3,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/suite-over-budget.err
    static let suiteOverBudget = #"""
apogee chat: suite vast needs 247056 MiB of this machine's 110100 MiB: root 41096 + vast 204936 + 1024 margin -- it does not fit; --force runs it anyway
"""#
    /// tests/business/reads/Fixtures/suites-models-status-cloud.json
    static let suiteStatusCloud = #"""
{"fields":[{"field":"suite","value":"cloud"},{"field":"chat","value":"claude   (via suite cloud)"},{"field":"embedding","value":"helper   (via suite cloud)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"(unset -- the chat's own backend)"},{"field":"footprint","value":"suite cloud\nclaude (chat): nothing held here\nhelper (embedding): 41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens, 0 MiB of adapters\ntotal: suite cloud needs 42120 MiB of this machine's 110100 MiB: helper 41096 + 1024 margin -- fits"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-fast.json
    static let suiteStatusFast = #"""
{"fields":[{"field":"suite","value":"fast"},{"field":"chat","value":"fastroot   (via suite fast)"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"fasthelper   (via suite fast)"},{"field":"footprint","value":"suite fast\nfastroot (chat): nothing held here\nfasthelper (utility): nothing held here\ntotal: suite fast: no member holds memory on this machine"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-pair.json
    static let suiteStatusPair = #"""
{"fields":[{"field":"suite","value":"pair"},{"field":"chat","value":"plain   (via suite pair)"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"fasthelper   (via suite pair)"},{"field":"footprint","value":"suite pair\nplain (chat): nothing held here\nfasthelper (utility): nothing held here\ntotal: suite pair: no member holds memory on this machine"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-shared.json
    static let suiteStatusShared = #"""
{"fields":[{"field":"suite","value":"shared"},{"field":"chat","value":"root   (via suite shared)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"scribe   (via suite shared)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"helper   (via suite shared)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"footprint","value":"suite shared\nroot (chat): 41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\nscribe (extraction): 136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\nhelper (utility): 136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens, 0 MiB of adapters\ntotal: suite shared needs 42392 MiB of this machine's 110100 MiB: root 41096 + scribe 136 (shares root's weights, 40960 MiB saved) + helper 136 (shares root's weights, 40960 MiB saved) + 1024 margin -- fits"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-vast.json
    static let suiteStatusVast = #"""
{"fields":[{"field":"suite","value":"vast"},{"field":"chat","value":"root   (via suite vast)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"vast   (via suite vast)   [local: 204800 MiB of weights, 136 MiB of cache]"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"(unset -- the chat's own backend)"},{"field":"footprint","value":"suite vast\nroot (chat): 41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\nvast (extraction): 204936 MiB -- 204800 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\ntotal: suite vast needs 247056 MiB of this machine's 110100 MiB: root 41096 + vast 204936 + 1024 margin -- it does not fit"}]}
"""#
}
#endif
