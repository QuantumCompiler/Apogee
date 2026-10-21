#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum LibraryDocuments {
    /// tests/business/reads/Fixtures/agents-list.json
    static let agentsList = #"""
{
 "data": [
  {
   "bundled": true,
   "collection": "",
   "description": "Offline, diff-first security review of the branch under review: findings with impact, remediation and CWE, and a verdict",
   "mcp": [],
   "model": "",
   "name": "security-review",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/security-review.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "security-review",
   "schemas": [
    "schemas/security-review-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": true,
   "collection": "",
   "description": "Categorized release notes (features, fixes, breaking changes) for a commit range or the branch under review",
   "mcp": [],
   "model": "",
   "name": "release-notes",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/release-notes.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "release-notes",
   "schemas": [
    "schemas/release-notes-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": true,
   "collection": "",
   "description": "Merge-readiness review of the branch under review against acceptance criteria and ADRs from a collection; peer review or self review with a paste-ready description",
   "mcp": [],
   "model": "",
   "name": "merge-request",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/merge-request.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "merge-request",
   "schemas": [
    "schemas/merge-request-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": true,
   "collection": "",
   "description": "Correctness and simplification review of a diff, a file or the branch under review: findings with location, severity and a suggested fix, and a verdict",
   "mcp": [],
   "model": "",
   "name": "code-review",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/code-review.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "code-review",
   "schemas": [
    "schemas/code-review-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": true,
   "collection": "",
   "description": "The behaviors a diff, a file or the branch under review adds that no test would notice breaking, each with a suggested test",
   "mcp": [],
   "model": "",
   "name": "test-gaps",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/test-gaps.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "test-gaps",
   "schemas": [
    "schemas/test-gaps-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": true,
   "collection": "",
   "description": "Documentation statements the branch under review made untrue, checked against a collection of the project's docs (--rag) and its files, each with an edit",
   "mcp": [],
   "model": "",
   "name": "doc-drift",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/doc-drift.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "doc-drift",
   "schemas": [
    "schemas/doc-drift-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": true,
   "collection": "",
   "description": "Minutes from a meeting transcript: decisions, action items with owner and due date, and open questions",
   "mcp": [],
   "model": "",
   "name": "meeting-minutes",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/meeting-minutes.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "meeting-minutes",
   "schemas": [
    "schemas/meeting-minutes-output.json"
   ],
   "tools": "read-only"
  },
  {
   "bundled": false,
   "collection": "",
   "description": "Sort incoming issues by area",
   "mcp": [],
   "model": "",
   "name": "triage",
   "output_format": "auto",
   "overrides_bundled": false,
   "prompts": [
    "prompts/triage.txt"
   ],
   "questions": false,
   "save_dir": "",
   "save_filename": "",
   "save_subdir": "triage",
   "schemas": [
    "schemas/triage-output.json"
   ],
   "tools": "read-only"
  }
 ],
 "object": "list"
}
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
    /// tests/business/reads/Fixtures/mcp-list.json
    static let mcpList = #"""
{
 "data": [
  {
   "args": [
    "/Users/someone/.apogee/training/scripts/code_exec_server.py"
   ],
   "command": "/opt/homebrew/bin/python3",
   "enabled": true,
   "env_set": false,
   "name": "code",
   "protocol_version": "2025-03-26",
   "state": "connected",
   "tools": [
    "run_python",
    "write_input"
   ]
  }
 ],
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/mcp-presets.json
    static let mcpPresets = #"""
{
 "data": [
  {
   "args": [
    "/Users/someone/.apogee/training/scripts/code_exec_server.py"
   ],
   "command": "/opt/homebrew/bin/python3",
   "description": "Runs Python for any model, local or cloud: a working directory kept for the session and deleted after it, the network off, each run bounded -- Apogee's code-execution server",
   "host_entry": {
    "name": "code",
    "server": {
     "args": [
      "/Users/someone/.apogee/training/scripts/code_exec_server.py"
     ],
     "command": "/opt/homebrew/bin/python3"
    }
   },
   "name": "code-exec",
   "provides": "run_python, write_input",
   "register": "apogee mcp create code --from code-exec",
   "registers_into_apogee": true,
   "runnable": true
  },
  {
   "args": [
    "__mcp-tools"
   ],
   "command": "/Users/taylor/.local/bin/apogee",
   "description": "Apogee's own read-only tools, served on stdio for another MCP host (a desktop assistant, an editor)",
   "host_entry": {
    "name": "apogee",
    "server": {
     "args": [
      "__mcp-tools"
     ],
     "command": "/Users/taylor/.local/bin/apogee"
    }
   },
   "name": "self",
   "provides": "the read-only native tools: files, git, notes, document search, graph navigation",
   "refusal": "'self' is Apogee's own tools, served for another MCP host: registered into Apogee it would serve Apogee the tools it already has natively. Give another host its entry instead (apogee mcp presets)",
   "registers_into_apogee": false,
   "runnable": true
  }
 ],
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/symphony-play.json
    static let symphonyPlay = #"""
{
 "object": "symphony.play",
 "output": "A tighter draft, for new contributors: what the tool does, then how to run it.",
 "stages": [
  {
   "answer": "1. The draft never says who it is for.\n2. The second paragraph repeats the first.",
   "backend": "writer",
   "cut": false,
   "name": "critique",
   "role": "utility",
   "seconds": 0.0
  },
  {
   "answer": "A tighter draft, for new contributors: what the tool does, then how to run it.",
   "backend": "writer",
   "cut": false,
   "name": "revise",
   "role": "chat",
   "seconds": 0.0
  }
 ],
 "suite": null,
 "symphony": "critique-revise"
}
"""#
}
#endif
