#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum GraphDocuments {
    /// tests/presentation/graph/Fixtures/stats.json
    static let stats = #"""
{"built":true,"chunks_with_mentions":0,"code_files":4,"code_files_by_language":{"python":4},"code_mentions":16,"collections":[],"communities":0,"communities_unsummarised":0,"edges":21,"edges_extracted":21,"edges_inferred":0,"embed_model":"","extract_model":"","failed_chunks":0,"graph":"demo","kind":"named","members":[],"membership_changed_from":[],"mentions":0,"nodes":16,"nodes_by_type":{"class":2,"file":4,"function":6,"name":4},"nodes_with_vectors":0,"object":"graph.stats","sources":["/Users/someone/src"],"stale_files":0,"total_chunks":0,"unresolved_names":4}
"""#
    /// tests/presentation/graph/Fixtures/query.json
    static let query = #"""
{"budget":1500,"entities":[{"description":"class HttpClient","end_line":3,"file":"ingest/fetch.py","hop":0,"line":1,"member":"src","name":"ingest.fetch.HttpClient","type":"class"},{"description":"class Runner","end_line":17,"file":"ingest/runner.py","hop":0,"line":4,"member":"src","name":"ingest.runner.Runner","type":"class"},{"description":"def _fetch(self, url)","end_line":17,"file":"ingest/runner.py","hop":0,"line":12,"member":"src","name":"ingest.runner.Runner._fetch","type":"function"},{"description":"Python file","end_line":3,"file":"ingest/fetch.py","hop":0,"line":1,"member":"src","name":"ingest/fetch.py","type":"file"},{"description":"def main()","end_line":5,"file":"ingest/cli.py","hop":1,"line":4,"member":"src","name":"ingest.cli.main","type":"function"},{"description":"def __init__(self, client: HttpClient)","end_line":6,"file":"ingest/runner.py","hop":1,"line":5,"member":"src","name":"ingest.runner.Runner.__init__","type":"function"},{"description":"def run_batch(self, batch)","end_line":10,"file":"ingest/runner.py","hop":1,"line":8,"member":"src","name":"ingest.runner.Runner.run_batch","type":"function"},{"description":"Python file","end_line":17,"file":"ingest/runner.py","hop":1,"line":1,"member":"src","name":"ingest/runner.py","type":"file"}],"graph":"demo","hops":1,"match":"names","max_entities":8,"object":"graph.query","question":"runner fetch","relations":[{"from":"ingest.cli.main","origin":"extracted","relation":"calls","to":"ingest.fetch.HttpClient"},{"from":"ingest.cli.main","origin":"extracted","relation":"calls","to":"ingest.runner.Runner"},{"from":"ingest.fetch.HttpClient","origin":"extracted","relation":"defined_in","to":"ingest/fetch.py"},{"from":"ingest.runner.Runner","origin":"extracted","relation":"defined_in","to":"ingest/runner.py"},{"from":"ingest.runner.Runner.__init__","origin":"extracted","relation":"defined_in","to":"ingest.runner.Runner"},{"from":"ingest.runner.Runner.__init__","origin":"extracted","relation":"references","to":"ingest.fetch.HttpClient"},{"from":"ingest.runner.Runner._fetch","origin":"extracted","relation":"defined_in","to":"ingest.runner.Runner"},{"from":"ingest.runner.Runner.run_batch","origin":"extracted","relation":"calls","to":"ingest.runner.Runner._fetch"},{"from":"ingest.runner.Runner.run_batch","origin":"extracted","relation":"defined_in","to":"ingest.runner.Runner"},{"from":"ingest/runner.py","origin":"extracted","relation":"imports","to":"ingest/fetch.py"}],"seeds":[{"end_line":17,"file":"ingest/runner.py","line":12,"member":"src","name":"ingest.runner.Runner._fetch","type":"function"},{"end_line":3,"file":"ingest/fetch.py","line":1,"member":"src","name":"ingest.fetch.HttpClient","type":"class"},{"end_line":3,"file":"ingest/fetch.py","line":1,"member":"src","name":"ingest/fetch.py","type":"file"},{"end_line":17,"file":"ingest/runner.py","line":4,"member":"src","name":"ingest.runner.Runner","type":"class"}],"truncated":false}
"""#
    /// tests/presentation/graph/Fixtures/explain.json
    static let explain = #"""
{"communities":[],"decisions":[],"degree":{"in":4,"out":1,"total":5},"description":"class Runner","graph":"demo","matched":"unqualified","max_per_relation":12,"mentions":1,"node":{"end_line":17,"file":"ingest/runner.py","line":4,"member":"src","name":"ingest.runner.Runner","type":"class"},"object":"graph.node","provenance":{"chunks":[],"code":[{"end_line":17,"file":"ingest/runner.py","line":4,"member":"src","role":"definition"}]},"relations":[{"direction":"in","neighbors":[{"at":"ingest/cli.py:5","end_line":5,"file":"ingest/cli.py","line":4,"member":"src","name":"ingest.cli.main","origin":"extracted","type":"function","weight":1}],"relation":"calls","total":1},{"direction":"out","neighbors":[{"at":"ingest/runner.py:4","end_line":17,"file":"ingest/runner.py","line":1,"member":"src","name":"ingest/runner.py","origin":"extracted","type":"file","weight":1}],"relation":"defined_in","total":1},{"direction":"in","neighbors":[{"at":"ingest/runner.py:5","end_line":6,"file":"ingest/runner.py","line":5,"member":"src","name":"ingest.runner.Runner.__init__","origin":"extracted","type":"function","weight":1},{"at":"ingest/runner.py:12","end_line":17,"file":"ingest/runner.py","line":12,"member":"src","name":"ingest.runner.Runner._fetch","origin":"extracted","type":"function","weight":1},{"at":"ingest/runner.py:8","end_line":10,"file":"ingest/runner.py","line":8,"member":"src","name":"ingest.runner.Runner.run_batch","origin":"extracted","type":"function","weight":1}],"relation":"defined_in","total":3}]}
"""#
    /// tests/business/reads/Fixtures/graph-prose-stats.json
    static let proseStats = #"""
{
 "built": true,
 "chunks_with_mentions": 4,
 "code_files": 0,
 "code_files_by_language": {},
 "code_mentions": 0,
 "communities": 1,
 "communities_unsummarised": 0,
 "edges": 5,
 "edges_extracted": 0,
 "edges_inferred": 5,
 "extract_model": "extractor",
 "failed_chunks": 0,
 "graph": "notes",
 "kind": "collection",
 "mentions": 13,
 "nodes": 4,
 "nodes_by_type": {
  "component": 2,
  "decision": 1,
  "system": 1
 },
 "nodes_with_vectors": 0,
 "object": "graph.stats",
 "stale_files": 0,
 "total_chunks": 4,
 "unresolved_names": 0
}
"""#
    /// tests/business/reads/Fixtures/graph-prose-report.json
    static let proseReport = #"""
{
 "communities": {
  "shown": [
   {
    "id": 1,
    "members": [
     {
      "name": "Compressor",
      "type": "component"
     },
     {
      "name": "Heat pump",
      "type": "system"
     },
     {
      "name": "Outdoor unit",
      "type": "component"
     }
    ],
    "size": 3,
    "summary": "The heat pump, its compressor and its outdoor unit: one machine moving heat indoors."
   }
  ],
  "total": 1,
  "unsummarised": 0
 },
 "decisions": {
  "shown": [
   {
    "decision": "Explain the outdoor unit in the heat pump guide. \u2014 Customers kept asking why the outdoor unit runs in winter.",
    "node": {
     "discipline": "product",
     "name": "kr-20261021T050657Z-c74d1a",
     "status": "shipped",
     "type": "decision"
    },
    "relations": [
     {
      "direction": "out",
      "neighbors": [
       {
        "name": "Compressor",
        "origin": "inferred",
        "type": "component",
        "weight": 1
       },
       {
        "name": "Heat pump",
        "origin": "inferred",
        "type": "system",
        "weight": 1
       },
       {
        "name": "Outdoor unit",
        "origin": "inferred",
        "type": "component",
        "weight": 1
       }
      ],
      "relation": "concerns",
      "total": 3
     }
    ]
   }
  ],
  "total": 1
 },
 "graph": "notes",
 "hubs": {
  "metric": "degree",
  "ranked": 4,
  "shown": [
   {
    "degree": {
     "in": 1,
     "out": 2,
     "total": 3
    },
    "mentions": 4,
    "node": {
     "name": "Heat pump",
     "type": "system"
    },
    "relations": [
     {
      "direction": "in",
      "neighbors": [
       {
        "discipline": "product",
        "name": "kr-20261021T050657Z-c74d1a",
        "origin": "inferred",
        "status": "shipped",
        "type": "decision",
        "weight": 1
       }
      ],
      "relation": "concerns",
      "total": 1
     },
     {
      "direction": "out",
      "neighbors": [
       {
        "description": "the compressor drives the cycle",
        "name": "Compressor",
        "origin": "inferred",
        "type": "component",
        "weight": 4
       },
       {
        "description": "the outdoor unit gathers heat",
        "name": "Outdoor unit",
        "origin": "inferred",
        "type": "component",
        "weight": 4
       }
      ],
      "relation": "contains",
      "total": 2
     }
    ]
   },
   {
    "degree": {
     "in": 0,
     "out": 3,
     "total": 3
    },
    "mentions": 1,
    "node": {
     "discipline": "product",
     "name": "kr-20261021T050657Z-c74d1a",
     "status": "shipped",
     "type": "decision"
    },
    "relations": [
     {
      "direction": "out",
      "neighbors": [
       {
        "name": "Compressor",
        "origin": "inferred",
        "type": "component",
        "weight": 1
       },
       {
        "name": "Heat pump",
        "origin": "inferred",
        "type": "system",
        "weight": 1
       },
       {
        "name": "Outdoor unit",
        "origin": "inferred",
        "type": "component",
        "weight": 1
       }
      ],
      "relation": "concerns",
      "total": 3
     }
    ]
   },
   {
    "degree": {
     "in": 2,
     "out": 0,
     "total": 2
    },
    "mentions": 4,
    "node": {
     "name": "Compressor",
     "type": "component"
    },
    "relations": [
     {
      "direction": "in",
      "neighbors": [
       {
        "discipline": "product",
        "name": "kr-20261021T050657Z-c74d1a",
        "origin": "inferred",
        "status": "shipped",
        "type": "decision",
        "weight": 1
       }
      ],
      "relation": "concerns",
      "total": 1
     },
     {
      "direction": "in",
      "neighbors": [
       {
        "description": "the compressor drives the cycle",
        "name": "Heat pump",
        "origin": "inferred",
        "type": "system",
        "weight": 4
       }
      ],
      "relation": "contains",
      "total": 1
     }
    ]
   },
   {
    "degree": {
     "in": 2,
     "out": 0,
     "total": 2
    },
    "mentions": 4,
    "node": {
     "name": "Outdoor unit",
     "type": "component"
    },
    "relations": [
     {
      "direction": "in",
      "neighbors": [
       {
        "discipline": "product",
        "name": "kr-20261021T050657Z-c74d1a",
        "origin": "inferred",
        "status": "shipped",
        "type": "decision",
        "weight": 1
       }
      ],
      "relation": "concerns",
      "total": 1
     },
     {
      "direction": "in",
      "neighbors": [
       {
        "description": "the outdoor unit gathers heat",
        "name": "Heat pump",
        "origin": "inferred",
        "type": "system",
        "weight": 4
       }
      ],
      "relation": "contains",
      "total": 1
     }
    ]
   }
  ]
 },
 "human_summary": "Graph \"notes\" holds 4 entities and 5 relations. Every relation was asserted by a model. Its busiest entity is Heat pump (system), with 3 relations. 1 community is stored; 1 has a summary. 1 decision record is attached.",
 "object": "graph.report",
 "origin": {
  "extracted": 0,
  "inferred": 5,
  "relations": 5
 },
 "orphans": {
  "shown": [],
  "total": 0
 },
 "overview": {
  "by_type": {
   "component": 2,
   "decision": 1,
   "system": 1
  },
  "entities": 4,
  "members": [
   ""
  ],
  "relations": 5,
  "unresolved_names": 0
 }
}
"""#
    /// tests/business/reads/Fixtures/graph-prose-explain.json
    static let proseExplain = #"""
{
 "communities": [
  {
   "id": 1,
   "size": 3,
   "summary": "The heat pump, its compressor and its outdoor unit: one machine moving heat indoors."
  }
 ],
 "decisions": [
  {
   "decision": "Explain the outdoor unit in the heat pump guide. \u2014 Customers kept asking why the outdoor unit runs in winter.",
   "discipline": "product",
   "name": "kr-20261021T050657Z-c74d1a",
   "status": "shipped",
   "type": "decision"
  }
 ],
 "degree": {
  "in": 1,
  "out": 2,
  "total": 3
 },
 "description": "moves heat from a cold place to a warm one",
 "graph": "notes",
 "matched": "exact",
 "max_per_relation": 12,
 "mentions": 4,
 "node": {
  "name": "Heat pump",
  "type": "system"
 },
 "object": "graph.node",
 "provenance": {
  "chunks": [
   {
    "chunk": 0,
    "collection": "",
    "source": "heat-pumps.md"
   },
   {
    "chunk": 0,
    "collection": "",
    "source": "solar.md"
   },
   {
    "chunk": 0,
    "collection": "",
    "source": "windows.md"
   },
   {
    "chunk": 0,
    "collection": "",
    "source": "kr-20261021T050657Z-c74d1a"
   }
  ],
  "code": []
 },
 "relations": [
  {
   "direction": "in",
   "neighbors": [
    {
     "discipline": "product",
     "name": "kr-20261021T050657Z-c74d1a",
     "origin": "inferred",
     "status": "shipped",
     "type": "decision",
     "weight": 1
    }
   ],
   "relation": "concerns",
   "total": 1
  },
  {
   "direction": "out",
   "neighbors": [
    {
     "description": "the compressor drives the cycle",
     "name": "Compressor",
     "origin": "inferred",
     "type": "component",
     "weight": 4
    },
    {
     "description": "the outdoor unit gathers heat",
     "name": "Outdoor unit",
     "origin": "inferred",
     "type": "component",
     "weight": 4
    }
   ],
   "relation": "contains",
   "total": 2
  }
 ]
}
"""#
}
#endif
