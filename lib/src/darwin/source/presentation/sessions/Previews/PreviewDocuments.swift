#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum SessionsDocuments {
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
    /// tests/business/reads/Fixtures/chats-list.json
    static let chatsList = #"""
{"data":[{"backend":"chatty","id":"20261022-182803-f9f6","model":"","name":"Planning A Note","turns":5,"updated":"2026-10-22T18:28:03Z"}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/chats-show.json
    static let chatsShow = #"""
{"attachments":[{"bytes":71,"files":1,"name":"/Users/someone/Documents/report.md"}],"backend":"chatty","id":"20261022-182803-f9f6","model":"","name":"Planning A Note","object":"chat","title":"Planning A Note","turns":[{"answer":{"text":"Then write the date first, as 2026-10-22.","tools":[]},"answered":true,"compacted":{"summary":"They asked how to write a note; a short list, the date first."},"turn":2,"user":{"attachments":[],"text":""}},{"answer":{"text":"The dev server listens on port 8080.","tools":[]},"answered":true,"turn":3,"user":{"attachments":[],"text":"Which port does the dev server use?"}},{"answer":{"text":"notes.md holds two items: **buy milk** and *call the plumber*.","tools":[{"name":"read_file","result_bytes":30,"summary":"{\"path\":\"Documents/notes.md\"}"}]},"answered":true,"turn":4,"user":{"attachments":[],"text":"What is in notes.md?"}},{"answer":{"text":"","tools":[]},"answered":false,"turn":4,"user":{"attachments":[],"text":"Is the launch still on?"}},{"answer":{"text":"The report says the launch moved to **November**:\n\n- the beta ends on 2026-10-31\n- the launch is on 2026-11-12","tools":[]},"answered":true,"turn":5,"user":{"attachments":[{"bytes":71,"files":1,"name":"/Users/someone/Documents/report.md"}],"text":"What does the report I attached say?"}}]}
"""#
    /// tests/data/protocol/Fixtures/machine-schema.json
    static let machineSchema = #"""
{
  "$defs": {
    "event_answer_delta": {
      "additionalProperties": true,
      "description": "A chunk of answer text.",
      "properties": {
        "text": {
          "description": "The text, to concatenate or display as it arrives.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "answer_delta"
        }
      },
      "required": [
        "type",
        "text"
      ],
      "type": "object"
    },
    "event_answer_end": {
      "additionalProperties": true,
      "description": "The answer ended.",
      "properties": {
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "answer_end"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "event_answer_start": {
      "additionalProperties": true,
      "description": "An answer begins.",
      "properties": {
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "answer_start"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "event_assembling": {
      "additionalProperties": true,
      "description": "A clip's frames going into it.",
      "properties": {
        "frames": {
          "description": "Frames going into the clip.",
          "type": "integer"
        },
        "scene": {
          "description": "In a composition: the scene this belongs to, from 1.",
          "type": "integer"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "assembling"
        }
      },
      "required": [
        "type",
        "frames"
      ],
      "type": "object"
    },
    "event_bytes": {
      "additionalProperties": true,
      "description": "Bytes a job has moved, and of how many when known.",
      "properties": {
        "count": {
          "description": "Files in the tree.",
          "type": "integer"
        },
        "done": {
          "description": "Bytes moved so far.",
          "type": "integer"
        },
        "file": {
          "description": "In a tree of files: the one moving.",
          "type": "string"
        },
        "index": {
          "description": "Its place in the tree, from 1.",
          "type": "integer"
        },
        "total": {
          "description": "Bytes in all; absent when unknown, never guessed.",
          "type": "integer"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "bytes"
        }
      },
      "required": [
        "type",
        "done"
      ],
      "type": "object"
    },
    "event_document": {
      "additionalProperties": true,
      "description": "A job's last event: the verb's own document.",
      "properties": {
        "document": {
          "description": "The verb's own document, whole: what --output-format json prints.",
          "type": "object"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "document"
        }
      },
      "required": [
        "type",
        "document"
      ],
      "type": "object"
    },
    "event_error": {
      "additionalProperties": true,
      "description": "A turn failed: ends it in place of a result.",
      "properties": {
        "message": {
          "description": "Why the turn failed.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "error"
        }
      },
      "required": [
        "type",
        "message"
      ],
      "type": "object"
    },
    "event_frames": {
      "additionalProperties": true,
      "description": "A clip's frames written.",
      "properties": {
        "frames": {
          "description": "Frames written.",
          "type": "integer"
        },
        "height": {
          "description": "Their height, in pixels.",
          "type": "integer"
        },
        "scene": {
          "description": "In a composition: the scene this belongs to, from 1.",
          "type": "integer"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "frames"
        },
        "width": {
          "description": "Their width, in pixels.",
          "type": "integer"
        }
      },
      "required": [
        "type",
        "frames",
        "width",
        "height"
      ],
      "type": "object"
    },
    "event_iteration": {
      "additionalProperties": true,
      "description": "A training iteration done, as the trainer reported it.",
      "properties": {
        "iteration": {
          "description": "Training iterations done.",
          "type": "integer"
        },
        "iterations": {
          "description": "Iterations in all; absent when the trainer did not say.",
          "type": "integer"
        },
        "loss": {
          "description": "The training loss the trainer reported.",
          "type": "number"
        },
        "lr": {
          "description": "The learning rate it reported.",
          "type": "number"
        },
        "stage": {
          "description": "In a pipeline: the stage this belongs to, from 1.",
          "type": "integer"
        },
        "throughput": {
          "description": "Iterations a second, as it reported.",
          "type": "number"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "iteration"
        }
      },
      "required": [
        "type",
        "iteration",
        "loss",
        "lr",
        "throughput"
      ],
      "type": "object"
    },
    "event_job": {
      "additionalProperties": true,
      "description": "A long job's first event: the verb and the versions.",
      "properties": {
        "protocol_version": {
          "description": "As the session event's.",
          "type": "integer"
        },
        "schema": {
          "description": "The vocabulary's version, a date.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "job"
        },
        "verb": {
          "description": "The verb running: models pull, video generate, ...",
          "type": "string"
        }
      },
      "required": [
        "type",
        "verb",
        "protocol_version",
        "schema"
      ],
      "type": "object"
    },
    "event_memory": {
      "additionalProperties": true,
      "description": "What a turn was handed from earlier conversations.",
      "properties": {
        "chats": {
          "description": "Past chats' summaries recalled for this turn.",
          "type": "integer"
        },
        "decisions": {
          "description": "Knowledge records recalled for this turn.",
          "type": "integer"
        },
        "sessions": {
          "description": "Each past chat recalled, in the order sent; absent when chats is 0.",
          "items": {
            "additionalProperties": true,
            "properties": {
              "id": {
                "description": "The chat's id, as chats list and --resume take it.",
                "type": "string"
              },
              "name": {
                "description": "Its title when it was summarised; empty when it had none.",
                "type": "string"
              },
              "updated": {
                "description": "When it was last updated before that, ISO 8601.",
                "type": "string"
              }
            },
            "required": [
              "id",
              "name",
              "updated"
            ],
            "type": "object"
          },
          "type": "array"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "memory"
        }
      },
      "required": [
        "type",
        "chats",
        "decisions"
      ],
      "type": "object"
    },
    "event_notice": {
      "additionalProperties": true,
      "description": "A line for the user that is neither progress nor an error.",
      "properties": {
        "text": {
          "description": "The text, to concatenate or display as it arrives.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "notice"
        }
      },
      "required": [
        "type",
        "text"
      ],
      "type": "object"
    },
    "event_pass": {
      "additionalProperties": true,
      "description": "A line of a cycle pass's narration, with where the pass is.",
      "properties": {
        "state": {
          "description": "Where the pass is: collecting, merging, training, gating, promoting -- or how it ended: skipped, failed, halted (the circuit breaker tripped on it), passed.",
          "type": "string"
        },
        "text": {
          "description": "The line, as the terminal says it.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "pass"
        }
      },
      "required": [
        "type",
        "state",
        "text"
      ],
      "type": "object"
    },
    "event_question": {
      "additionalProperties": true,
      "description": "A question that waits for answer lines.",
      "properties": {
        "detail": {
          "description": "An outbound question: the whole URL. A write_file or edit_file question: the change as a unified diff, cut at 32 KB with a last line saying so.",
          "type": "string"
        },
        "kind": {
          "description": "permission for the permission gate's question; absent for ask_user.",
          "type": "string"
        },
        "outbound": {
          "description": "A permission question about data leaving the machine.",
          "type": "boolean"
        },
        "questions": {
          "description": "Each to be answered by one answer line, in order.",
          "items": {
            "additionalProperties": true,
            "properties": {
              "header": {
                "description": "A short label for the question.",
                "type": "string"
              },
              "multi_select": {
                "description": "Whether more than one option may be chosen.",
                "type": "boolean"
              },
              "options": {
                "description": "Offered answers -- free text is always accepted too.",
                "items": {
                  "additionalProperties": true,
                  "properties": {
                    "description": {
                      "description": "What choosing it means.",
                      "type": "string"
                    },
                    "label": {
                      "description": "The option, as an answer would give it.",
                      "type": "string"
                    }
                  },
                  "required": [
                    "label",
                    "description"
                  ],
                  "type": "object"
                },
                "type": "array"
              },
              "question": {
                "description": "The question.",
                "type": "string"
              }
            },
            "required": [
              "header",
              "question",
              "multi_select",
              "options"
            ],
            "type": "object"
          },
          "type": "array"
        },
        "target": {
          "description": "A permission question: its path, command or host.",
          "type": "string"
        },
        "tool": {
          "description": "A permission question: the tool asking.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "question"
        }
      },
      "required": [
        "type",
        "questions"
      ],
      "type": "object"
    },
    "event_ready": {
      "additionalProperties": true,
      "description": "A job's runtime loaded, and on what.",
      "properties": {
        "device": {
          "description": "Where the runtime runs: cuda, mps, cpu.",
          "type": "string"
        },
        "dtype": {
          "description": "The precision it loaded in, when the driver says.",
          "type": "string"
        },
        "engine": {
          "description": "The engine the run takes: torch or mlx.",
          "type": "string"
        },
        "mode": {
          "description": "A clip's: t2v or i2v.",
          "type": "string"
        },
        "scene": {
          "description": "In a composition: the scene this belongs to, from 1.",
          "type": "integer"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "ready"
        }
      },
      "required": [
        "type",
        "device"
      ],
      "type": "object"
    },
    "event_result": {
      "additionalProperties": true,
      "description": "Ends a turn, with the whole answer.",
      "properties": {
        "finish_reason": {
          "description": "Why the turn ended: stop, length, tool_calls, content_filter, cancelled (a cancel ended it), other -- more may be added.",
          "type": "string"
        },
        "model": {
          "description": "The model that answered.",
          "type": "string"
        },
        "text": {
          "description": "The whole answer, reasoning never in it.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "result"
        },
        "usage": {
          "additionalProperties": true,
          "description": "Absent when the provider reported none -- absent is not zero.",
          "properties": {
            "input_tokens": {
              "description": "Tokens sent.",
              "type": "integer"
            },
            "output_tokens": {
              "description": "Tokens generated.",
              "type": "integer"
            }
          },
          "required": [
            "input_tokens",
            "output_tokens"
          ],
          "type": "object"
        }
      },
      "required": [
        "type",
        "text",
        "model",
        "finish_reason"
      ],
      "type": "object"
    },
    "event_scene": {
      "additionalProperties": true,
      "description": "A composition's scene begun: speaking, rendering or its asset.",
      "properties": {
        "scene": {
          "description": "The scene, from 1.",
          "type": "integer"
        },
        "scenes": {
          "description": "Scenes in the composition.",
          "type": "integer"
        },
        "state": {
          "description": "speaking (its narration), rendering (its clip) or asset (its asset taken as it is).",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "scene"
        }
      },
      "required": [
        "type",
        "scene",
        "scenes",
        "state"
      ],
      "type": "object"
    },
    "event_segment": {
      "additionalProperties": true,
      "description": "A segment of speech made.",
      "properties": {
        "index": {
          "description": "Which segment of speech, from 1.",
          "type": "integer"
        },
        "of": {
          "description": "Segments in all.",
          "type": "integer"
        },
        "scene": {
          "description": "In a composition: the scene this belongs to, from 1.",
          "type": "integer"
        },
        "seconds": {
          "description": "Its length, as the driver counted.",
          "type": "number"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "segment"
        }
      },
      "required": [
        "type",
        "index",
        "of",
        "seconds"
      ],
      "type": "object"
    },
    "event_session": {
      "additionalProperties": true,
      "description": "Once, first, unprompted: the protocol version, the model, the session's capabilities.",
      "properties": {
        "capabilities": {
          "additionalProperties": true,
          "description": "What this session can do.",
          "properties": {
            "accepts": {
              "description": "The line types this session reads on stdin.",
              "items": {
                "type": "string"
              },
              "type": "array"
            },
            "ask": {
              "description": "Whether ask_user and the permission prompt reach the driver as question events.",
              "type": "boolean"
            },
            "events": {
              "description": "Every event type this build can write.",
              "items": {
                "type": "string"
              },
              "type": "array"
            },
            "schema": {
              "description": "The vocabulary's version, a date.",
              "type": "string"
            },
            "tools": {
              "description": "Whether the model can call tools in this session.",
              "type": "boolean"
            }
          },
          "required": [
            "events",
            "accepts",
            "tools",
            "ask",
            "schema"
          ],
          "type": "object"
        },
        "chat_id": {
          "description": "The chat this session writes, as chats list, chats show and --resume name it; absent when it writes none (chat --private).",
          "type": "string"
        },
        "model": {
          "description": "The backend answering.",
          "type": "string"
        },
        "next_turn": {
          "description": "A session of user lines: the number its next turn will carry.",
          "type": "integer"
        },
        "private": {
          "description": "true for chat --private: nothing of the session is ever written.",
          "type": "boolean"
        },
        "protocol_version": {
          "description": "Bumped only when an existing event's meaning changes.",
          "type": "integer"
        },
        "type": {
          "const": "session"
        }
      },
      "required": [
        "type",
        "protocol_version",
        "model",
        "capabilities"
      ],
      "type": "object"
    },
    "event_sources": {
      "additionalProperties": true,
      "description": "The excerpts retrieval sent the model, before the answer.",
      "properties": {
        "sources": {
          "description": "Each excerpt sent, in the order sent.",
          "items": {
            "additionalProperties": true,
            "properties": {
              "file": {
                "description": "The file as its index names it.",
                "type": "string"
              },
              "lines": {
                "description": "First and last line, when the excerpt records them.",
                "items": {
                  "type": "integer"
                },
                "type": "array"
              },
              "pages": {
                "description": "First and last page, when the excerpt records them.",
                "items": {
                  "type": "integer"
                },
                "type": "array"
              },
              "retriever": {
                "description": "lexical, vector or hybrid: the scale the score is on; scales do not compare.",
                "type": "string"
              },
              "score": {
                "description": "Its score, on its retriever's scale.",
                "type": "number"
              },
              "times": {
                "description": "A recording's span, in seconds.",
                "items": {
                  "type": "number"
                },
                "type": "array"
              }
            },
            "required": [
              "file",
              "score",
              "retriever"
            ],
            "type": "object"
          },
          "type": "array"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "sources"
        }
      },
      "required": [
        "type",
        "sources"
      ],
      "type": "object"
    },
    "event_stage": {
      "additionalProperties": true,
      "description": "A step of a job's chain -- a pipeline's stage, as its manifest records it.",
      "properties": {
        "count": {
          "description": "How many steps the chain has.",
          "type": "integer"
        },
        "cumulative_score": {
          "description": "A pipeline's stage: its cumulative gate's score, 0 to 1, once the gate has scored it.",
          "type": "number"
        },
        "index": {
          "description": "Which step of the chain -- a pipeline's stage -- from 1.",
          "type": "integer"
        },
        "label": {
          "description": "The step, in the chain's words: a pipeline's, the stage's name.",
          "type": "string"
        },
        "pipeline_run_id": {
          "description": "A pipeline's stage: the pipeline run.",
          "type": "string"
        },
        "run_id": {
          "description": "A pipeline's stage: its own run, once it has one -- what train eval and train promote take.",
          "type": "string"
        },
        "state": {
          "description": "started, done or failed; a pipeline's stage, its manifest's word -- pending, training, evaluating, fusing, passed, failed.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "stage"
        }
      },
      "required": [
        "type",
        "index",
        "count",
        "label",
        "state"
      ],
      "type": "object"
    },
    "event_step": {
      "additionalProperties": true,
      "description": "A denoising step done.",
      "properties": {
        "scene": {
          "description": "In a composition: the scene this belongs to, from 1.",
          "type": "integer"
        },
        "step": {
          "description": "Denoising steps done.",
          "type": "integer"
        },
        "steps": {
          "description": "Steps in all.",
          "type": "integer"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "step"
        }
      },
      "required": [
        "type",
        "step",
        "steps"
      ],
      "type": "object"
    },
    "event_task_finished": {
      "additionalProperties": true,
      "description": "A task ended.",
      "properties": {
        "reason": {
          "description": "Why, in words.",
          "type": "string"
        },
        "status": {
          "description": "How the task ended.",
          "type": "string"
        },
        "task": {
          "description": "The task's final view.",
          "type": "object"
        },
        "task_id": {
          "description": "The task.",
          "type": "string"
        },
        "transition": {
          "description": "The transition, as the ledger wrote it.",
          "type": "object"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "task_finished"
        }
      },
      "required": [
        "type",
        "status",
        "reason",
        "task",
        "task_id",
        "transition"
      ],
      "type": "object"
    },
    "event_task_grant": {
      "additionalProperties": true,
      "description": "A call a task's grant let through.",
      "properties": {
        "by": {
          "description": "The authority: grant.",
          "type": "string"
        },
        "round": {
          "description": "The round the grant was used in.",
          "type": "integer"
        },
        "target": {
          "description": "What it was let at.",
          "type": "string"
        },
        "task_id": {
          "description": "The task.",
          "type": "string"
        },
        "tool": {
          "description": "The tool the grant let through.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "task_grant"
        }
      },
      "required": [
        "type",
        "task_id",
        "round",
        "tool",
        "target",
        "by"
      ],
      "type": "object"
    },
    "event_task_plan": {
      "additionalProperties": true,
      "description": "A task's plan turn started, or its plan was recorded.",
      "properties": {
        "plan": {
          "description": "Once recorded: the plan.",
          "type": "string"
        },
        "round": {
          "description": "The plan turn.",
          "type": [
            "object",
            "null"
          ]
        },
        "task_id": {
          "description": "The task.",
          "type": "string"
        },
        "transition": {
          "description": "The transition, as the ledger wrote it.",
          "type": "object"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "task_plan"
        }
      },
      "required": [
        "type",
        "round",
        "task_id",
        "transition"
      ],
      "type": "object"
    },
    "event_task_round": {
      "additionalProperties": true,
      "description": "A task's round started or ended.",
      "properties": {
        "checks": {
          "description": "Once the round ended: each check's state.",
          "items": {
            "type": "object"
          },
          "type": "array"
        },
        "round": {
          "description": "The round.",
          "type": [
            "object",
            "null"
          ]
        },
        "rounds_used": {
          "description": "Once the round ended: rounds used of the budget.",
          "type": "integer"
        },
        "task_id": {
          "description": "The task.",
          "type": "string"
        },
        "transition": {
          "description": "The transition, as the ledger wrote it.",
          "type": "object"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "task_round"
        }
      },
      "required": [
        "type",
        "round",
        "task_id",
        "transition"
      ],
      "type": "object"
    },
    "event_task_started": {
      "additionalProperties": true,
      "description": "A task run started or resumed.",
      "properties": {
        "history": {
          "description": "Every transition before this one.",
          "items": {
            "type": "object"
          },
          "type": "array"
        },
        "resumed": {
          "description": "Whether this run resumed the task.",
          "type": "boolean"
        },
        "task": {
          "description": "The task's whole view.",
          "type": "object"
        },
        "task_id": {
          "description": "The task.",
          "type": "string"
        },
        "transition": {
          "description": "The transition, as the ledger wrote it.",
          "type": "object"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "task_started"
        }
      },
      "required": [
        "type",
        "resumed",
        "history",
        "task",
        "task_id",
        "transition"
      ],
      "type": "object"
    },
    "event_thinking": {
      "additionalProperties": true,
      "description": "The model began reasoning; again with budget_reached when it ran out.",
      "properties": {
        "budget_reached": {
          "description": "The reasoning reached its thinking budget and was ended there.",
          "type": "boolean"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "thinking"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "event_thinking_delta": {
      "additionalProperties": true,
      "description": "A chunk of reasoning -- droppable.",
      "properties": {
        "text": {
          "description": "The text, to concatenate or display as it arrives.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "thinking_delta"
        }
      },
      "required": [
        "type",
        "text"
      ],
      "type": "object"
    },
    "event_todo": {
      "additionalProperties": true,
      "description": "The session's todo, whole, as the model last wrote it.",
      "properties": {
        "items": {
          "description": "The whole list, in order; empty when cleared.",
          "items": {
            "additionalProperties": true,
            "properties": {
              "state": {
                "description": "pending, active (being done now) or done.",
                "type": "string"
              },
              "text": {
                "description": "What is to be done, in a short line.",
                "type": "string"
              }
            },
            "required": [
              "text",
              "state"
            ],
            "type": "object"
          },
          "type": "array"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "todo"
        }
      },
      "required": [
        "type",
        "items"
      ],
      "type": "object"
    },
    "event_tool_status": {
      "additionalProperties": true,
      "description": "A tool running, or another model call the turn makes.",
      "properties": {
        "text": {
          "description": "The text, to concatenate or display as it arrives.",
          "type": "string"
        },
        "turn": {
          "description": "In a driven chat or execute: the turn this event belongs to, the number of its user line.",
          "type": "integer"
        },
        "type": {
          "const": "tool_status"
        }
      },
      "required": [
        "type",
        "text"
      ],
      "type": "object"
    },
    "inbound": {
      "allOf": [
        {
          "if": {
            "properties": {
              "type": {
                "const": "user"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_user"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "answer"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_answer"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "attach"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_attach"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "hello"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_hello"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "cancel"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_cancel"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "detach"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_detach"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "set"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_set"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "compact"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/line_compact"
          }
        }
      ],
      "description": "A line a driver writes.",
      "properties": {
        "type": {
          "type": "string"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "line_answer": {
      "additionalProperties": true,
      "description": "An answer to a pending question.",
      "properties": {
        "text": {
          "description": "The answer to the oldest unanswered question.",
          "type": "string"
        },
        "type": {
          "const": "answer"
        }
      },
      "required": [
        "type",
        "text"
      ],
      "type": "object"
    },
    "line_attach": {
      "additionalProperties": true,
      "description": "Something to attach to the chat.",
      "properties": {
        "graph": {
          "description": "A folder of code: code or off, over the config's.",
          "type": "string"
        },
        "path": {
          "description": "A file, folder or glob to attach.",
          "type": "string"
        },
        "type": {
          "const": "attach"
        }
      },
      "required": [
        "type",
        "path"
      ],
      "type": "object"
    },
    "line_cancel": {
      "additionalProperties": true,
      "description": "Stop the turn in flight, as Ctrl-C would.",
      "properties": {
        "type": {
          "const": "cancel"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "line_compact": {
      "additionalProperties": true,
      "description": "Compact the conversation's history now, as /compact does.",
      "properties": {
        "type": {
          "const": "compact"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "line_detach": {
      "additionalProperties": true,
      "description": "Detach an attachment, as /detach does.",
      "properties": {
        "path": {
          "description": "The attachment to detach, by the name it was attached as.",
          "type": "string"
        },
        "type": {
          "const": "detach"
        }
      },
      "required": [
        "type",
        "path"
      ],
      "type": "object"
    },
    "line_hello": {
      "additionalProperties": true,
      "description": "An optional first line: who is driving.",
      "properties": {
        "client": {
          "additionalProperties": true,
          "description": "Who is driving.",
          "properties": {
            "name": {
              "description": "The host application.",
              "type": "string"
            },
            "version": {
              "description": "Its version.",
              "type": "string"
            }
          },
          "required": [],
          "type": "object"
        },
        "type": {
          "const": "hello"
        },
        "wants": {
          "description": "Words for what the host wants -- recorded, not acted on.",
          "items": {
            "type": "string"
          },
          "type": "array"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "line_set": {
      "additionalProperties": true,
      "description": "Move the session's run settings from the next turn, as their slash commands do.",
      "properties": {
        "effort": {
          "description": "An effort level or auto, as /effort takes it.",
          "type": "string"
        },
        "model": {
          "description": "A backend, a roster model, or backend:model, as /model takes it.",
          "type": "string"
        },
        "system": {
          "description": "The system prompt, replaced whole, as /system takes it.",
          "type": "string"
        },
        "temperature": {
          "description": "The sampling temperature, as /temperature takes it.",
          "type": [
            "number",
            "string"
          ]
        },
        "think": {
          "description": "on, off or auto, as /think takes it.",
          "type": "string"
        },
        "type": {
          "const": "set"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    },
    "line_user": {
      "additionalProperties": true,
      "description": "A user turn.",
      "properties": {
        "text": {
          "description": "The user's message: one turn.",
          "type": "string"
        },
        "type": {
          "const": "user"
        }
      },
      "required": [
        "type",
        "text"
      ],
      "type": "object"
    },
    "outbound": {
      "allOf": [
        {
          "if": {
            "properties": {
              "type": {
                "const": "session"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_session"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "thinking"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_thinking"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "thinking_delta"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_thinking_delta"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "memory"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_memory"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "sources"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_sources"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "tool_status"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_tool_status"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "notice"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_notice"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "todo"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_todo"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "answer_start"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_answer_start"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "answer_delta"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_answer_delta"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "answer_end"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_answer_end"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "result"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_result"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "question"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_question"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "error"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_error"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "task_started"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_task_started"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "task_plan"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_task_plan"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "task_round"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_task_round"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "task_grant"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_task_grant"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "task_finished"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_task_finished"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "job"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_job"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "stage"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_stage"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "bytes"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_bytes"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "ready"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_ready"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "step"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_step"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "frames"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_frames"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "assembling"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_assembling"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "segment"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_segment"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "scene"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_scene"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "iteration"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_iteration"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "pass"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_pass"
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "document"
              }
            },
            "required": [
              "type"
            ]
          },
          "then": {
            "$ref": "#/$defs/event_document"
          }
        }
      ],
      "description": "A line Apogee writes.",
      "properties": {
        "type": {
          "type": "string"
        }
      },
      "required": [
        "type"
      ],
      "type": "object"
    }
  },
  "$id": "https://github.com/QuantumCompiler/Apogee/machine-schema/2026-10-25",
  "$ref": "#/$defs/outbound",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "description": "One JSON object per line. The root validates a line Apogee writes on stdout; #/$defs/inbound a line a driver writes on stdin. An unknown type validates, and every line admits fields it does not name: ignore what you do not know.",
  "title": "Apogee machine mode",
  "x-apogee": {
    "protocol_version": 1,
    "schema": "2026-10-25"
  }
}
"""#
    /// tests/business/reads/Fixtures/chats-list-two.json
    static let chatsListTwo = #"""
{"data":[{"backend":"actor","id":"20261026-142409-5159","model":"","name":"Dev Server Port","turns":2,"updated":"2026-10-26T14:24:09Z"},{"backend":"actor","id":"20261026-142409-07b7","model":"","name":"Dev Server Port","turns":2,"updated":"2026-10-26T14:24:09Z"}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/chats-info.txt
    static let chatsInfo = #"""
id:        20261026-142409-5159
title:     Dev Server Port
backend:   actor
started:   2026-10-26T14:24:09Z
updated:   2026-10-26T14:24:09Z
turns:     2
messages:  4
"""#
    /// tests/business/reads/Fixtures/chats-recall.txt
    static let chatsRecall = #"""
recall holds 2 chat summaries in /Users/someone/.apogee/memory/chats.db
"""#
    /// tests/business/reads/Fixtures/chats-compact.txt
    static let chatsCompact = #"""
compacted 20261026-142409-5159: 4 messages into 2, by compactor
"""#
    /// tests/business/reads/Fixtures/store-list-kinds-registered.json
    static let kindsRegistered = #"""
{"data":[{"architecture":"gemma4_assistant","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"drafter","model":"Gemma4-E4B-Assistant/mlx/4d5e6f708192","note":"a drafter: it runs beside its base model, never alone","profile":"gemma3","quant":"bf16","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"32768-token window (the default)"},{"architecture":"kokoro","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"voice","model":"Kokoro-82M/mlx/6f708192a3b4","note":"a voice model: 'apogee voice say' speaks with it, never a chat backend","profile":"unprofiled","quant":"precision not stated","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"-"},{"architecture":"musicgen","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"music","model":"MusicGen-Small/mlx/708192a3b4c5","note":"kind 'music', not one this apogee knows: nothing here runs it","profile":"unprofiled","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"32768-token window (the default)"},{"architecture":"ti2v","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"video","model":"Wan2.2-TI2V-5B/mlx/5e6f708192a3","note":"a video model: 'apogee video generate' runs it, never a chat backend","profile":"unprofiled","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"4 file(s) on record","window":"-"},{"architecture":"gemma3_text","backend":"EmbeddingGemma-300M-bf16","backend_type":"mlx","format":"mlx","group":"mlx","kind":"embedding","model":"EmbeddingGemma-300M/mlx/3c4d5e6f7081","note":"there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx","profile":"gemma3","quant":"bf16","roles":"","section":"local","source":"local","state":"no environment","type":"model","verified":"5 file(s) on record","window":"2048-token window (the default; trained for 2048)"},{"architecture":"gemma4","backend":"Gemma4-E4B-4bit","backend_type":"mlx","format":"mlx","group":"mlx","kind":"vision","model":"Gemma4-E4B/mlx/2b3c4d5e6f70","note":"there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx","profile":"gemma3","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"local","state":"no environment","type":"model","verified":"6 file(s) on record","window":"32768-token window (the default; trained for 131072)"},{"architecture":"qwen3","backend":"Qwen3-8B-4bit","backend_type":"mlx","format":"mlx","group":"mlx","kind":"chat","model":"Qwen3-8B/mlx/1a2b3c4d5e6f","note":"there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx","profile":"qwen3","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"local","state":"no environment","type":"model","verified":"5 file(s) on record","window":"32768-token window (the default; trained for 40960)"}],"folded":0,"object":"list"}
"""#
    /// tests/data/protocol/Fixtures/switched-to-local.jsonl
    static let switchedToLocal = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-25","tools":true},"chat_id":"20261026-172749-d4cb","model":"claude","next_turn":1,"protocol_version":1,"type":"session"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"The dev ","turn":1,"type":"answer_delta"}
{"text":"server l","turn":1,"type":"answer_delta"}
{"text":"istens o","turn":1,"type":"answer_delta"}
{"text":"n port 8","turn":1,"type":"answer_delta"}
{"text":"080.","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"claude","text":"The dev server listens on port 8080.","turn":1,"type":"result"}
{"text":"switched to Gemma4-26B-Instruct -- from the next turn","type":"notice"}
{"turn":2,"type":"thinking"}
{"turn":2,"type":"answer_start"}
{"text":"Gemma he","turn":2,"type":"answer_delta"}
{"text":"re, on t","turn":2,"type":"answer_delta"}
{"text":"his mach","turn":2,"type":"answer_delta"}
{"text":"ine: the","turn":2,"type":"answer_delta"}
{"text":" dev ser","turn":2,"type":"answer_delta"}
{"text":"ver list","turn":2,"type":"answer_delta"}
{"text":"ens on p","turn":2,"type":"answer_delta"}
{"text":"ort 8080","turn":2,"type":"answer_delta"}
{"text":".","turn":2,"type":"answer_delta"}
{"turn":2,"type":"answer_end"}
{"finish_reason":"stop","model":"Gemma4-26B-Instruct","text":"Gemma here, on this machine: the dev server listens on port 8080.","turn":2,"type":"result"}
"""#
    /// tests/business/reads/Fixtures/chats-list-switched.json
    static let chatsListSwitched = #"""
{"data":[{"backend":"Gemma4-26B-Instruct","id":"20261026-172749-d4cb","model":"","name":"Dev Server Port","turns":2,"updated":"2026-10-26T17:27:51Z"}],"object":"list"}
"""#
    /// tests/data/protocol/Fixtures/resumed-under-suite-m29.jsonl
    static let resumedUnderSuiteM29 = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-26","tools":true},"chat_id":"20261027-002140-dfa1","model":"plain","next_turn":2,"protocol_version":1,"suite":"pair","type":"session"}
{"text":"suite pair: no member holds memory on this machine","type":"notice"}
{"text":"utility — restating the follow-up to rank the tools, with fasthelper","turn":2,"type":"tool_status"}
{"turn":2,"type":"thinking"}
{"turn":2,"type":"answer_start"}
{"text":"PLAIN-SA","turn":2,"type":"answer_delta"}
{"text":"YS hello","turn":2,"type":"answer_delta"}
{"turn":2,"type":"answer_end"}
{"finish_reason":"stop","model":"plain","text":"PLAIN-SAYS hello","turn":2,"type":"result"}
"""#
    /// tests/data/protocol/Fixtures/execute-orchestrating.jsonl
    static let executeOrchestrating = #"""
{"capabilities":{"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"ask":true,"events":["session","thinking","thinking_delta","memory","sources","tool_status","notice","todo","answer_start","answer_delta","answer_end","result","question","error","task_started","task_plan","task_round","task_grant","task_finished","job","stage","bytes","ready","step","frames","assembling","segment","scene","iteration","pass","document"],"schema":"2026-10-26","tools":true},"chat_id":"20261027-002140-630d","model":"fastroot","next_turn":1,"orchestrating":true,"protocol_version":1,"suite":"orch","type":"session"}
{"text":"suite orch: no member holds memory on this machine","type":"notice"}
{"turn":1,"type":"thinking"}
{"turn":1,"type":"answer_start"}
{"text":"FASTROOT","turn":1,"type":"answer_delta"}
{"text":"-SAYS he","turn":1,"type":"answer_delta"}
{"text":"llo","turn":1,"type":"answer_delta"}
{"turn":1,"type":"answer_end"}
{"finish_reason":"stop","model":"fastroot","text":"FASTROOT-SAYS hello","turn":1,"type":"result"}
"""#
}
#endif
