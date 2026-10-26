#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum KnowledgeDocuments {
    /// tests/business/reads/Fixtures/knowledge-list.json
    static let knowledgeList = #"""
{
 "data": [
  {
   "decision": "Lead release notes with user-felt changes; process last.",
   "discipline": "product",
   "downstream_link": "",
   "id": "kr-20261021T045954Z-0084c5",
   "intent": "Release notes read better when they lead with what changed for the person running it.",
   "provenance": {
    "attribution": "Bob",
    "source": "chat"
   },
   "raw_ref": "/Users/someone/.apogee/knowledge/raw/kr-20261021T045954Z-0084c5.md",
   "status": "shipped",
   "timestamp": "2026-10-21T04:59:54.408295Z"
  },
  {
   "decision": "Default the window to the model's trained length, capped by free memory.",
   "discipline": "eng",
   "downstream_link": "PROJ-42",
   "id": "kr-20261021T045944Z-3346a9",
   "intent": "Local models should load at a window the machine can hold.",
   "provenance": {
    "attribution": "Grace Hopper",
    "source": "review"
   },
   "raw_ref": "/Users/someone/.apogee/knowledge/raw/kr-20261021T045944Z-3346a9.md",
   "status": "shipped",
   "timestamp": "2026-10-21T04:59:44.477387Z"
  },
  {
   "decision": "Remove the cancel button from checkout.",
   "discipline": "ux",
   "downstream_link": "",
   "id": "kr-20261021T045944Z-1e2354",
   "intent": "We dropped the cancel button because testers kept mistaking it for back.",
   "provenance": {
    "attribution": "Ada Lovelace",
    "source": "meeting"
   },
   "raw_ref": "/Users/someone/.apogee/knowledge/raw/kr-20261021T045944Z-1e2354.md",
   "status": "rejected",
   "timestamp": "2026-10-21T04:59:44.463192Z"
  }
 ],
 "db": "knowledge",
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/knowledge-query.json
    static let knowledgeQuery = #"""
{
 "records": [
  {
   "record": {
    "decision": "Remove the cancel button from checkout.",
    "discipline": "ux",
    "downstream_link": "",
    "id": "kr-20261021T045944Z-1e2354",
    "intent": "We dropped the cancel button because testers kept mistaking it for back.",
    "provenance": {
     "attribution": "Ada Lovelace",
     "source": "meeting"
    },
    "raw_ref": "/Users/someone/.apogee/knowledge/raw/kr-20261021T045944Z-1e2354.md",
    "status": "rejected",
    "timestamp": "2026-10-21T04:59:44.463192Z"
   },
   "score": 0.5962886409906911
  },
  {
   "record": {
    "decision": "Default the window to the model's trained length, capped by free memory.",
    "discipline": "eng",
    "downstream_link": "PROJ-42",
    "id": "kr-20261021T045944Z-3346a9",
    "intent": "Local models should load at a window the machine can hold.",
    "provenance": {
     "attribution": "Grace Hopper",
     "source": "review"
    },
    "raw_ref": "/Users/someone/.apogee/knowledge/raw/kr-20261021T045944Z-3346a9.md",
    "status": "shipped",
    "timestamp": "2026-10-21T04:59:44.477387Z"
   },
   "score": 1.542680546962327e-06
  },
  {
   "record": {
    "decision": "Lead release notes with user-felt changes; process last.",
    "discipline": "product",
    "downstream_link": "",
    "id": "kr-20261021T045954Z-0084c5",
    "intent": "Release notes read better when they lead with what changed for the person running it.",
    "provenance": {
     "attribution": "Bob",
     "source": "chat"
    },
    "raw_ref": "/Users/someone/.apogee/knowledge/raw/kr-20261021T045954Z-0084c5.md",
    "status": "shipped",
    "timestamp": "2026-10-21T04:59:54.408295Z"
   },
   "score": 9.656479224850218e-07
  }
 ],
 "reranked": false,
 "retriever": "lexical"
}
"""#
    /// tests/business/reads/Fixtures/collections-list.json
    static let collectionsList = #"""
{
 "data": [
  {
   "chunks": 3,
   "name": "knowledge",
   "sources": 3
  },
  {
   "chunks": 5,
   "name": "notes",
   "sources": 5
  }
 ],
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/collection-info.json
    static let collectionInfo = #"""
{
 "chunks": 5,
 "collection": "notes",
 "index": "ok",
 "path": "/Users/someone/.apogee/embeddings/notes.db",
 "schema": 6,
 "sources": [
  "graph://community/1",
  "heat-pumps.md",
  "kr-20261021T050657Z-c74d1a",
  "solar.md",
  "windows.md"
 ],
 "vectors": null
}
"""#
    /// tests/business/reads/Fixtures/collection-query.txt
    static let collectionQuery = #"""
0.000  [lexical]  heat-pumps.md#0
    # Heat pumps  A heat pump moves heat from a cold place to a warm one. In winter the outdoor unit pul…
0.000  [lexical]  graph://community/1#0
    The heat pump, its compressor and its outdoor unit: one machine moving heat indoors.
0.000  [lexical]  windows.md#0
    # Windows  Triple glazing halves the heat lost through a window. Frames matter as much as glass. 
0.000  [lexical]  kr-20261021T050657Z-c74d1a#0
    Customers kept asking why the outdoor unit runs in winter.  Decision: Explain the outdoor unit in th…
0.000  [lexical]  solar.md#0
    # Solar roofs  Panels make the most power at noon. A battery carries the surplus into the evening.  …
"""#
    /// tests/business/reads/Fixtures/knowledge-list-fresh.err
    static let knowledgeListFresh = #"""
apogee knowledge: no knowledge collection named 'knowledge' -- capture a record first with 'apogee knowledge capture'
"""#
    /// tests/business/reads/Fixtures/collections-list-fresh.json
    static let collectionsListFresh = #"""
{"data":[],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/knowledge-list-one.json
    static let knowledgeListOne = #"""
{"data":[{"decision":"Remove the cancel button from checkout.","discipline":"ux","downstream_link":"","id":"kr-20261026T142411Z-2e8272","intent":"We dropped the cancel button because testers kept mistaking it for back.","provenance":{"attribution":"Ada Lovelace","source":"meeting"},"raw_ref":"/Users/someone/.apogee/knowledge/raw/kr-20261026T142411Z-2e8272.md","status":"shipped","timestamp":"2026-10-26T14:24:11.732993Z"}],"db":"knowledge","object":"list"}
"""#
    /// tests/business/reads/Fixtures/knowledge-info-raw.txt
    static let knowledgeInfoRaw = #"""
ID:          kr-20261026T142411Z-2e8272
Intent:      We dropped the cancel button because testers kept mistaking it for back.
Decision:    Remove the cancel button from checkout.
Status:      shipped
Discipline:  ux
Link:        -
Source:      meeting
Attribution: Ada Lovelace
Raw:         /Users/someone/.apogee/knowledge/raw/kr-20261026T142411Z-2e8272.md
Captured:    2026-10-26T14:24:11.732993Z

-- Raw conversation --
Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it.
"""#
    /// tests/business/reads/Fixtures/knowledge-info-raw-unavailable.txt
    static let knowledgeInfoRawUnavailable = #"""
ID:          kr-20261026T142411Z-2e8272
Intent:      We dropped the cancel button because testers kept mistaking it for back.
Decision:    Remove the cancel button from checkout.
Status:      shipped
Discipline:  ux
Link:        -
Source:      meeting
Attribution: Ada Lovelace
Raw:         /Users/someone/.apogee/knowledge/raw/kr-20261026T142411Z-2e8272.md
Captured:    2026-10-26T14:24:11.732993Z

Raw conversation: (unavailable)
"""#
}
#endif
