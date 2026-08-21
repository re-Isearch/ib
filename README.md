## This is the respositiory for re-Isearch.

### NOTE:

IB (re-Isearch) no longer will compile with nearly "any" C++ compiler ever made but increasingly uses C++17+ constructs. The decision was motivated by the observation that platforms that don't have a modern C++ compiler are quite rarethese days and anyway highly unlikely to be used in 2026 for enterprize search. Given that Schmate (the vector extensions) exploits modern C++ and SIMD instructions it made no longer any sense to continue to be language constrained.  For those that need to run on archaic platforms the re-Isearch distribution still exists albeit now frozen.. 

The increasing focus is for the next generation of LLM and RL agents.  Agents need more than keyword matching. They need to express relationships: this term occurs inside that field, these concepts appear near each other, this phrase comes before that phrase, these facts belong to the same structural element. Positional and structural operators let an agent ask about the shape of information, not merely its presence.

LLMs, by contrast, tend to see retrieved text as relatively flat. Once content is turned into tokens, chunks, or embeddings, much of the original document structure—fields, containment, adjacency, order, hierarchy—can become weak or implicit. An LLM may infer those relationships, but inference is not the same as querying them precisely.

Structural and positional search gives the agent a way to preserve that information before generation: retrieve the right relationship, not just the right words. For agentic search, operators such as NEAR, BEFORE, PEER, WITHIN, and field constraints are therefore not syntactic luxuries; they are tools for turning a flat language model into a much more precise information-seeking system.

This is what we call "Agentic RAG 2.0”
- RAG 1.0: retrieve chunks, then ask the LLM to reason over them.
- Agentic RAG 2.0: let the agent actively construct and refine expressive retrieval plans using Boolean, structural, positional, scoring, and relaxation operators.

The key idea is that the agent is no longer just consuming retrieved context. It is programming the retrieval process:
"RAG gave LLMs documents. Agentic RAG 2.0 gives agents a retrieval algebra."



### NEWS

August 21 2026

Beyond many significant lexical speed-ups (some quite dramatic), new normalization algorithms (like BM25 to keep BEIR happy) we've added for agents several new binary operators to the CoreQuarry/IB query algebra: MAYBE, PROMOTE and DEMOTE

They are designed to express something conventional Boolean search does poorly: the difference between what must match, what should influence ranking, and how a query should gracefully relax when the corpus cannot satisfy its strongest interpretation.

* A B MAYBE is a symmetric, corpus-aware relaxation operator. It returns records matching both A and B when possible. If there are no common matches, it returns whichever operand has the larger result set, with score used to break ties.  In other words: try the conjunction; if the corpus cannot support it, choose the stronger fallback automatically.

* A B PROMOTE is directional: A B PROMOTE reads as “A promotes B.” The result set remains B, but records that also match A receive a score increase using A’s score. Importantly, A contributes no hit evidence.

* A B DEMOTE is its inverse: reads as “A demotes B.” The result set remains B, while records also matching A are down-scored. Again, A does not become search evidence.

That distinction is deliberate. PROMOTE and DEMOTE operate on the ranking plane, not the evidence plane. They can influence ordering without manufacturing hits, increasing term counts, satisfying REDUCE, affecting proximity calculations, or otherwise pretending that a preference was part of the original match.

#### Why agents love these operators

Most search syntaxes were designed for humans typing short queries into a box. AI agents have a different problem: they can construct structured query expressions and need precise ways to express intent.

An agent often wants to say:<PRE>
These concepts define relevance.
These concepts are preferences.
These concepts are negative preferences.
Try this stricter interpretation first,
but relax intelligently if the collection cannot satisfy it.</PRE>

With ordinary AND, OR, and NOT, those distinctions are difficult to express without expanding the query into increasingly complicated Boolean expressions or contaminating the evidence used by downstream ranking models.

With the new operators, an agent can express them directly.<PRE>wolf dog PROMOTE</PRE> or <PRE>spam dog DEMOTE</PRE>

And: <PRE>distributed "search engine" MAYBE</PRE> means "Use the joint interpretation if the collection supports it; otherwise retain the broader interpretation with the strongest corpus support."

This makes the query language more than a Boolean filter. It becomes a small retrieval algebra in which membership, evidence, ranking preference, structure, and relaxation can all be represented explicitly.

That is particularly useful for agent-generated search: the agent can build a query AST that says not only what to search for, but also how strongly each idea should participate in the search and what to do when the ideal interpretation fails.

MAYBE, PROMOTE, and DEMOTE are small operators, but they open up a much richer vocabulary for machine-generated queries.


Jul 26 2026

Loads of optimizations. New: extended the capacity.

<PRE>Physical Index Capacities for this 64-bit edition:
  Index id:    32-bit
  Max Input:   16384 Tbytes (Total of all files)
  Max Words:   1099511 trillion (optimized), >8796093 trillion (non-optimized)
  Max Unique:  135324 trillion words (optimized), >8796093 trillion (non-optimized)
  Word Freq:   Maximum same as "Max Words"
  Max Records: 16 million.
  Min. disk requirements: some fixed and variable amounts plus each
    record (168 bytes); word (8); unique word (~32); field (16).
Virtual Database Search Capacities:
  Max Input:   aprox. 4 Exabytes
  Max Words:   unlimited (limit only imposed by physical index)
  Max Unique:  unlimited (limit only imposed by physical index)
  Max Indexes: 255
  Max Records: 4278 million (Total all indexes)
Preset Per-Record Limits:
  Max Key:     48 characters
  Max Doctype Child Names: 15 characters</PRE>

Added an option: BUILD_LARGE_INDEX

While the 32-bit Ids are sufficient to index the entire English Wikipedia (7 million articles) we were asked if we could not extend the capacity. BUILD_LARGE_INDEX unleashes 64-bit index IDs to effectively "unlimit" the number of records (on paper Seventy-two quadrillion). More than enough to index the entire Library of Congress's 175 million items-- albeit not in terribly good practice (instead use a subject or domain segmentation and use virtual indexes to afford a better querry routing). This is **not** the default since we can't imagine many use cases (other than logs to which this engine is sub-optimal) where more than 16 million records in a single index or 4 billion in a virtual index provides a real limit especially given our focus to run on bare metal consumer hardware).

Jul 2026

Moving more and more away from generic C++ towards increasingly using C++17+. The base old version stays as re-Isearch..

June 4 2026

The core re-Isearch engine is NOW published as "ib". This is a key component of the release of CoreQuarry which contains: ib (formerly re-Isearch), Schmate, bert.cpp, hnswlib etc.

The reason for the re-naming under re-Isearch of the components is to make it clear that they are useable seperately but also to allow the development to start to diverge slightly from its focus on "any system". This allows us to start to use SIMD code in the lexical search kernel and switch to a cmake build system which for 99% of the users is 100% better.

May 23 2026

Things now getting ready for distribution. The use of the bert.cpp and ggml tensor library (0.12) reduced indexing time on Apple M1 by more than 900%. Our bert.cpp is now being extended to support both sentence-piece and word-piece tokenizers.

We still have some key items on our TODO/WISHLIST..
 - Cleaning up the build system.
 - Testing and benchmarks on NVIDIA Embedded and x86 machines: We've been focusing on Apple M-series hardware.
 - Caching appends to speed-up indexing by uses delayed batch processing. 
 - Support for the MRLQ model (that is MRL + RaBitQ)
 - Documentation / Handbooks. These READ.ME files have a lot and there is the re-Isearch handbook but..


May 18 2026

We decided, after much consideration, to move over the the latest ggml tensor library. The performance advantages were just too overwhelming to be held back for the upcoming release. To this end we heavily refactored bert.cpp. The new bert.cpp is built on the latest ggml (0.11.1 or 0.12). We've tried to design to support most BERT architecture models.

Rather than support all the accelertors in our first release we decided to keep to CUDA, Metal, Vulkan and CPU. This covers no less than 95% of all possible hosts-- probably 99.99%. Its also what we can exhaustively test against. Adding additional accelerators, as supported by ggml backends, is a relatively simple matter so should real demand exist (and are prepared to actively engage in testing and benchmarking) we will happily extend things-- its literally just few a few lines of code for each.

That's the good news. The bad news is that we've dropped support for the GGML model file format.  Instead of routing ggml files through bert.cpp and gguf through llama.cpp we now route classical BERT models through bert.cpp and models such as EuroBERT through llama.cpp.

More good news is that the old ggml models can be easily converted to gguf. And now with the Feb 2026 merger of GGML.ai into HuggingFace we can expect intensified GGUF support and significant synergies in ggml development.

With our shift:  We've also improved our support bge and nomic BERT. 

May 11 2026

We've added vector encoding detection to the JSON document handlers (via METADOC). This way fields that
have been defined with text encoded embeddings (hex, base64 or JSON-style arrays) get automatically detected as
vector field types (currently HNSW). The vectorization already identified these strings and by-passes
the model to push these into the graph.

Given the significant time and resources to generate the embeddings this is in many use cases highly
desirable: the JSON gets the embeddings pre-calculated. This significantly reduces the ingest timings
(as much as 15-fold).

May 7 2026

We are getting ready to finally release later this month. 
- Build system is now CMake
- Plugins are now in a sub-directory of doctypes/ 
- Extremely performant vector search using SIMD for search (HNSW) and accelerators (among others, CUDA, Metal, Vulkan) for the embedding models (GGML and GGUF formats).
- Quantization, Pass-through, etc. have now been fully tested with real-world data. 
- Configuration editor and standalone tool for use also as debugger for vector indexes.
- Support for JSON ingest. This includes not just Pure-JSON (which supports multiple JSON records in a single file), JSON-NL (New-line JSON often used for logs), JSON-LD (Linked Data) and EJSON (including inner-mappings to understand MongoDb JSON). JSON also supports datatype discover for a large number of the data types the core understands including: bounding-box (spherical), date, numerical, date, date-range, .... 

April 24 2026

The evolving version of re-Isearch+Schmate is getting ready for release-- its already pre-release. It now supports all the magic from Schmate using an interface layer. This allows Schmate standalone to continue to not just be used outside of re-Isearch but enables Schmate to search even in vector indexes created by re-Isearchi, viz. without the query and lexical baggage.

Towards this aim and making build easier we are transitioning towards CMAKE.. Re-Isearch without the vector can continue to use Makefiles such it be demanded by the platform but as the vector search demands a bit more computational resources (embeddings and HNSW vector search) we consider it reasonable to expect cmake. 

Please note: The vector variant (VECTOR_INDEX enabled in compile) is a pre-release. Its not yet ready for production deployment.

##### Building ####
From the root distribution directory create a sub-directory (build etc.). From that direction cmake ..  followed by make.

cmake provides a few options.


### Project re-isearch:
a novel multimodal search and retrieval engine using mathematical models and algorithms different from the all-too-common inverted index (popularized by Salton in the 1960s). The design allows it to have, in practice, effectively no limits on the frequency of words, term length, number of fields or complexity of structured data and support even overlap--- where fields or structures cross other's boundaries (common examples are quotes, line/sentences, biblical verse, annotations). Its model enables a completely flexible unit of retrieval and modes of search.

Despite being a new project it has a long and esteemed history reaching back into the 1990s. Previous versions were widely adopted and used in hundreds of public search sites, including many high profile projects such as the U.S. Patent and Trademark Office (USPTO) patent search, the Federal Geographic Data Clearinghouse (FGDC), the NASA Global Change Master Directory, the NASA EOS Guide System, the NASA Catalog Interoperability Project, the astronomical pre-print service based at the Space Telescope Science Institute, The PCT Electronic Gazette at the World Intellectual Property Organization (WIPO), the Australian National Genomic Information Service (ANGIS), the SAGE Project of the Special Collections Department at Emory University, Eco Companion Australasia (an environmental geospatial resources catalog), European Space Organization, the Open Directory Project, numerous governmental portals and ... 

Featues/Uses
* Low-code ETL / "Any-to-Any" architecture
* No need for a “middle layer” of content manipulation code. Instead of getting URLs from a search engine, fetching documents, parsing them, and navigating the DOMs to find required elements, it lets you simply request the elements you need and they are returned directly.
* Handles a wide range of document formats (from Atom to JSON to XML including many legacy formats such as Medline flat file, USPTO Greenbook, SGML etc) including “live” data.
* Support for UTF8 as well as "legacy" 8-bit character set encoded documents (ISO 8859-x formats).
* Powerful Search (Structure, Objects, Spatial) / Relevancy Engine
* NoSQL Datastore
* Useful for Analytics
* Useful for Recommendation / Autosuggestion 
* Embeddable in products (comparatively low resource  demands)
* Customization. 
* Support Peer-to-Peer and Federated architectures.
* Runs on a wide range of hardware and operating systems
* State of the Art Vector engine (Project Schmate)
* Freely available under a permissive software license. 


Despite its wealth of features it has a comparatively small memory footprint (previous version have run on 32-bit machines with as little as 8 MB physical RAM) making it suitable for appliances. It has also been designed to try to impose a minimal computing impact on the host.
Rather than run multiple threads and a high CPU workload it’s strategy is to be fast but not at the cost of other processes, heat or increased energy consumption.


### This Repository 

This is the main central repository for re-Isearch development.

It contains the engine as a freely available and completely open-source (and multiplatform) C++ library, bindings for other languages (such as Python) and some reference sample code using the library in some of these languages.

Under [doctypes/](https://github.com/re-Isearch/re-Isearch/tree/master/doctype) one can see the native doctypes supported.


## Building, installing, developing
For information on building, installing, developing and using the system please consult the handbook in [docs/](https://github.com/re-Isearch/re-Isearch/blob/master/docs/re-Isearch-Handbook.pdf).

A basic cheat-sheet is in [INSTALLATION](./INSTALLATION)

In the directory bin/ and lib/ are binaries of standalone tools compiled on Ubuntu 18.04.2 LTS and targetting Intel Skylake or newer processors. They are included solely to enable fast software evaluations.

## Copyrights, attributions and acknowledgements 
Portions Copyright (c) 1995 CNIDR/MCNC, (c) 1995-2011 BSn/Munich (BSn.com); (c) 2011-2020 NONMONOTONIC Networks (NONMONOTONIC.net); Copyright (c) 2020-23 Edward C. Zimmermann and (c) 2024-26 the re-iSearch project. Is is made available and licensed under the Apache 2.0 license: see LICENSE

The software has a lot of history (as one can see from the above copyright). For the historical last public release: [Isearch](https://github.com/edzimmermann/Isearch-1.14)

This project was funded through the NGI0 Discovery Fund, a fund established by NLnet with financial support from the European Commission's Next Generation Internet programme, under the aegis of DG Communications Networks, Content and Technology under grant agreement No 825322.



<IMG SRC="https://nlnet.nl/image/logo_nlnet.svg" ALT="NLnet Foundation" height=100> <IMG SRC="https://nlnet.nl/logo/NGI/NGIZero-green.hex.svg" ALT="NGI0 Search" height=100> &nbsp; &nbsp; <IMG SRC="https://ngi.eu/wp-content/uploads/sites/77/2017/10/bandiera_stelle.png" ALT="EU" height=100>

