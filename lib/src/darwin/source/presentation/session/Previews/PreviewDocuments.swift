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
}
#endif
