#include "result_store.hpp"

#include <common/logger.h>
#include <mongoc/mongoc.h>

#include <cstdint>
#include <ctime>
#include <cstdio>
#include <mutex>
#include <string>

namespace
{

constexpr const char* RESULTS_COLLECTION = "results";

std::once_flag    g_init_flag;
mongoc_client_pool_t* g_pool    = nullptr;
std::string       g_db_name;
std::mutex        g_state_mutex;

// Helper: insert one BSON document into the results collection (no lock held).
static bool insert_document(mongoc_client_pool_t* pool, const std::string& db, bson_t* doc)
{
    mongoc_client_t* client = mongoc_client_pool_pop(pool);
    if (client == nullptr)
        return false;

    mongoc_collection_t* coll = mongoc_client_get_collection(client, db.c_str(), RESULTS_COLLECTION);

    bson_error_t error;
    const bool ok = mongoc_collection_insert_one(coll, doc, nullptr, nullptr, &error);
    if (!ok)
        LOG_WARNING_MSG("[Result Store] Insert failed: %s", error.message);

    mongoc_collection_destroy(coll);
    mongoc_client_pool_push(pool, client);
    return ok;
}

} // namespace

namespace server
{

std::string format_timestamp_iso(std::int64_t timestamp_ms)
{
    std::time_t seconds = static_cast<std::time_t>(timestamp_ms / 1000);
    int millis = static_cast<int>(timestamp_ms % 1000);
    if (millis < 0)
    {
        millis += 1000;
        --seconds;
    }

    std::tm utc_tm {};
    if (gmtime_r(&seconds, &utc_tm) == nullptr)
        return "1970-01-01T00:00:00.000Z";

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  utc_tm.tm_year + 1900,
                  utc_tm.tm_mon + 1,
                  utc_tm.tm_mday,
                  utc_tm.tm_hour,
                  utc_tm.tm_min,
                  utc_tm.tm_sec,
                  millis);
    return buffer;
}

void init_result_store(const std::string& mongo_uri, const std::string& database_name)
{
    std::call_once(g_init_flag, []() { mongoc_init(); });

    bson_error_t error;
    mongoc_uri_t* uri = mongoc_uri_new_with_error(mongo_uri.c_str(), &error);
    if (uri == nullptr)
    {
        LOG_WARNING_MSG("[Result Store] Invalid Mongo URI, store disabled: %s", error.message);
        return;
    }

    mongoc_client_pool_t* pool = mongoc_client_pool_new(uri);
    mongoc_uri_destroy(uri);

    if (pool == nullptr)
    {
        LOG_WARNING_MSG("[Result Store] Failed to create client pool, store disabled");
        return;
    }

    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_pool != nullptr)
        mongoc_client_pool_destroy(g_pool);
    g_pool   = pool;
    g_db_name = database_name;
    LOG_INFO_MSG("[Result Store] MongoDB configured (db=%s)", database_name.c_str());
}

void save_flow_result(const std::string& source,
                      const std::string& sink,
                      int node_count,
                      double max_flow,
                      double execution_time_ms,
                      const std::string& timestamp,
                      bool use_openmp)
{
    mongoc_client_pool_t* pool_snap;
    std::string db_snap;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_pool == nullptr) return;
        pool_snap = g_pool;
        db_snap   = g_db_name;
    }

    bson_t* doc = bson_new();
    BSON_APPEND_UTF8(doc, "algorithm", "fulfillment-flow");

    bson_t req;
    BSON_APPEND_DOCUMENT_BEGIN(doc, "request", &req);
    BSON_APPEND_UTF8(&req, "source", source.c_str());
    BSON_APPEND_UTF8(&req, "sink",   sink.c_str());
    bson_append_document_end(doc, &req);

    bson_t res;
    BSON_APPEND_DOCUMENT_BEGIN(doc, "result", &res);
    BSON_APPEND_DOUBLE(&res, "max_flow", max_flow);
    bson_append_document_end(doc, &res);

    BSON_APPEND_INT32(doc,  "node_count",        node_count);
    BSON_APPEND_DOUBLE(doc, "execution_time_ms", execution_time_ms);
    BSON_APPEND_BOOL(doc,   "use_openmp",        use_openmp);
    BSON_APPEND_UTF8(doc,   "timestamp",         timestamp.c_str());

    insert_document(pool_snap, db_snap, doc);
    bson_destroy(doc);
}

void save_circuit_result(const std::string& start,
                         const std::vector<std::string>& subgraph_node_ids,
                         const CircuitResult& circuit_result,
                         const std::string& timestamp,
                         bool use_openmp)
{
    mongoc_client_pool_t* pool_snap;
    std::string db_snap;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_pool == nullptr) return;
        pool_snap = g_pool;
        db_snap   = g_db_name;
    }

    bson_t* doc = bson_new();
    BSON_APPEND_UTF8(doc, "algorithm", "fulfillment-circuit");

    bson_t req;
    BSON_APPEND_DOCUMENT_BEGIN(doc, "request", &req);
    BSON_APPEND_UTF8(&req, "start", start.c_str());
    bson_t nodes_arr;
    BSON_APPEND_ARRAY_BEGIN(&req, "subgraph_node_ids", &nodes_arr);
    char idx_buf[24];
    for (std::size_t i = 0; i < subgraph_node_ids.size(); ++i)
    {
        std::snprintf(idx_buf, sizeof(idx_buf), "%zu", i);
        BSON_APPEND_UTF8(&nodes_arr, idx_buf, subgraph_node_ids[i].c_str());
    }
    bson_append_array_end(&req, &nodes_arr);
    bson_append_document_end(doc, &req);

    bson_t res;
    BSON_APPEND_DOCUMENT_BEGIN(doc, "result", &res);
    BSON_APPEND_BOOL(&res,  "has_circuit", circuit_result.has_circuit);
    BSON_APPEND_INT32(&res, "node_count",  static_cast<int32_t>(subgraph_node_ids.size()));
    bson_t circuits_arr;
    BSON_APPEND_ARRAY_BEGIN(&res, "circuits", &circuits_arr);
    char ci_buf[24], cj_buf[24];
    for (std::size_t i = 0; i < circuit_result.circuits.size(); ++i)
    {
        std::snprintf(ci_buf, sizeof(ci_buf), "%zu", i);
        bson_t inner;
        BSON_APPEND_ARRAY_BEGIN(&circuits_arr, ci_buf, &inner);
        for (std::size_t j = 0; j < circuit_result.circuits[i].size(); ++j)
        {
            std::snprintf(cj_buf, sizeof(cj_buf), "%zu", j);
            const std::string& node_name =
                subgraph_node_ids.at(static_cast<std::size_t>(circuit_result.circuits[i][j]));
            BSON_APPEND_UTF8(&inner, cj_buf, node_name.c_str());
        }
        bson_append_array_end(&circuits_arr, &inner);
    }
    bson_append_array_end(&res, &circuits_arr);
    BSON_APPEND_DOUBLE(&res, "execution_time_ms", circuit_result.execution_time_ms);
    bson_append_document_end(doc, &res);

    BSON_APPEND_INT32(doc,  "node_count",  static_cast<int32_t>(subgraph_node_ids.size()));
    BSON_APPEND_BOOL(doc,   "use_openmp",  use_openmp);
    BSON_APPEND_UTF8(doc,   "timestamp",   timestamp.c_str());

    insert_document(pool_snap, db_snap, doc);
    bson_destroy(doc);
}

std::string get_all_results()
{
    mongoc_client_pool_t* pool_snap;
    std::string db_snap;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        pool_snap = g_pool;
        db_snap   = g_db_name;
    }

    if (pool_snap == nullptr || db_snap.empty())
        return R"({"status":"ok","results":[]})";

    mongoc_client_t* client = mongoc_client_pool_pop(pool_snap);
    if (client == nullptr)
        return R"({"status":"ok","results":[]})";

    mongoc_collection_t* coll =
        mongoc_client_get_collection(client, db_snap.c_str(), RESULTS_COLLECTION);

    bson_t* opts = BCON_NEW("sort", "{", "timestamp", BCON_INT32(-1), "timestamp_ms", BCON_INT32(-1), "}");
    bson_t filter;
    bson_init(&filter);
    mongoc_cursor_t* cursor = mongoc_collection_find_with_opts(coll, &filter, opts, nullptr);
    bson_destroy(&filter);
    bson_destroy(opts);

    std::string json_array = "[";
    const bson_t* rdoc;
    bool first = true;
    while (mongoc_cursor_next(cursor, &rdoc))
    {
        std::size_t len = 0;
        char* str = bson_as_relaxed_extended_json(rdoc, &len);
        if (str != nullptr)
        {
            if (!first) json_array += ',';
            json_array.append(str, len);
            bson_free(str);
            first = false;
        }
    }
    json_array += ']';

    bson_error_t cursor_err;
    if (mongoc_cursor_error(cursor, &cursor_err))
        LOG_ERROR_MSG("[Result Store] Cursor error: %s", cursor_err.message);

    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(coll);
    mongoc_client_pool_push(pool_snap, client);

    return std::string(R"({"status":"ok","results":)") + json_array + '}';
}

} // namespace server