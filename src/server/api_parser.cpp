#include "api_parser.hpp"

#include <cJSON.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace server
{

static void extract_string(cJSON* obj, const char* key, std::string& out)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring != nullptr)
    {
        out = item->valuestring;
    }
}

static void extract_bool(cJSON* obj, const char* key, bool& out, bool def_val)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsBool(item))
    {
        out = cJSON_IsTrue(item);
    }
    else
    {
        out = def_val;
    }
}

static void extract_double(cJSON* obj, const char* key, double& out, double def_val)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsNumber(item))
    {
        out = item->valuedouble;
    }
    else
    {
        out = def_val;
    }
}

static void extract_string_array(cJSON* arr, std::vector<std::string>& out)
{
    if (!arr || !cJSON_IsArray(arr))
        return;

    int size = cJSON_GetArraySize(arr);
    for (int i = 0; i < size; ++i)
    {
        cJSON* item = cJSON_GetArrayItem(arr, i);
        if (item && cJSON_IsString(item) && item->valuestring != nullptr)
        {
            out.push_back(item->valuestring);
        }
    }
}

static bool is_blank_json_body(const std::string& json_body)
{
    return json_body.find_first_not_of(" \t\r\n") == std::string::npos;
}

MapParseResult parse_map_json(const std::string& json_body)
{
    MapParseResult result;
    result.total_received = 0;
    result.total_discarded = 0;

    cJSON* root = cJSON_Parse(json_body.c_str());
    if (!root)
    {
        throw std::runtime_error("Invalid JSON format");
    }

    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        throw std::runtime_error("Expected a JSON array of map nodes");
    }

    int array_size = cJSON_GetArraySize(root);
    result.total_received = array_size;

    for (int i = 0; i < array_size; ++i)
    {
        cJSON* node_json = cJSON_GetArrayItem(root, i);
        if (!node_json || !cJSON_IsObject(node_json))
        {
            result.total_discarded++;
            continue;
        }

        std::string node_type;
        extract_string(node_json, "node_type", node_type);

        bool is_active = false;
        extract_bool(node_json, "is_active", is_active, false);

        bool is_secure = false;
        extract_bool(node_json, "is_secure", is_secure, false);
        if ((node_type != "fulfillment_center" && node_type != "market") || !is_active || !is_secure)
        {
            result.total_discarded++;
            continue;
        }

        MapNode node;
        node.node_type = node_type;
        node.is_active = is_active;
        node.is_secure = is_secure;

        extract_string(node_json, "node_id", node.node_id);
        extract_string(node_json, "node_name", node.node_name);
        extract_string(node_json, "node_description", node.node_description);

        cJSON* loc_json = cJSON_GetObjectItemCaseSensitive(node_json, "node_location");
        if (loc_json && cJSON_IsObject(loc_json))
        {
            extract_double(loc_json, "latitude", node.location.latitude, 0.0);
            extract_double(loc_json, "longitude", node.location.longitude, 0.0);
        }
        else
        {
            node.location.latitude = 0.0;
            node.location.longitude = 0.0;
        }

        cJSON* tags_json = cJSON_GetObjectItemCaseSensitive(node_json, "node_tags");
        extract_string_array(tags_json, node.node_tags);

        cJSON* conn_json = cJSON_GetObjectItemCaseSensitive(node_json, "connections");
        if (conn_json && cJSON_IsArray(conn_json))
        {
            int conn_size = cJSON_GetArraySize(conn_json);
            for (int k = 0; k < conn_size; ++k)
            {
                cJSON* edge_item = cJSON_GetArrayItem(conn_json, k);
                if (edge_item && cJSON_IsObject(edge_item))
                {
                    MapEdge edge;
                    extract_string(edge_item, "to", edge.to);
                    extract_string(edge_item, "connection_type", edge.connection_type);
                    extract_double(edge_item, "base_weight", edge.base_weight, 1.0);

                    cJSON* cond_json = cJSON_GetObjectItemCaseSensitive(edge_item, "connection_conditions");
                    extract_string_array(cond_json, edge.connection_conditions);

                    node.connections.push_back(edge);
                }
            }
        }

        result.nodes.push_back(node);
    }

    cJSON_Delete(root);
    return result;
}

FlowRequest parse_flow_request_json(const std::string& json_body)
{
    cJSON* root = cJSON_Parse(json_body.c_str());
    if (!root)
    {
        throw std::runtime_error("Invalid JSON format");
    }

    cJSON* source_item = cJSON_GetObjectItemCaseSensitive(root, "source");
    cJSON* sink_item = cJSON_GetObjectItemCaseSensitive(root, "sink");

    if (!source_item || !cJSON_IsString(source_item) || !sink_item || !cJSON_IsString(sink_item))
    {
        cJSON_Delete(root);
        throw std::runtime_error("Missing or invalid 'source' or 'sink' fields");
    }

    FlowRequest req;
    req.source = source_item->valuestring;
    req.sink = sink_item->valuestring;

    cJSON_Delete(root);
    return req;
}

CircuitRequest parse_circuit_request_json(const std::string& json_body)
{
    CircuitRequest req;

    if (is_blank_json_body(json_body))
    {
        return req;
    }

    cJSON* root = cJSON_Parse(json_body.c_str());
    if (!root)
    {
        throw std::runtime_error("Invalid JSON format");
    }

    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        throw std::runtime_error("Expected a JSON object");
    }

    cJSON* start_item = cJSON_GetObjectItemCaseSensitive(root, "start");
    if (start_item != nullptr)
    {
        if (!cJSON_IsString(start_item) || start_item->valuestring == nullptr)
        {
            cJSON_Delete(root);
            throw std::runtime_error("Invalid 'start' field");
        }

        req.start = start_item->valuestring;
    }

    cJSON_Delete(root);
    return req;
}

std::string build_flow_response_json(const FlowRequest& request, int node_count, const FlowResult& flow_result,
                                     const std::string& timestamp, bool use_openmp)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "source", request.source.c_str());
    cJSON_AddStringToObject(root, "sink", request.sink.c_str());
    cJSON_AddNumberToObject(root, "node_count", node_count);
    cJSON_AddNumberToObject(root, "max_flow", flow_result.max_flow);
    cJSON_AddNumberToObject(root, "execution_time_ms", flow_result.execution_time_ms);
    cJSON_AddBoolToObject(root, "use_openmp", use_openmp ? 1 : 0);
    cJSON_AddStringToObject(root, "timestamp", timestamp.c_str());

    char* raw_json = cJSON_PrintUnformatted(root);
    std::string response =
        raw_json != nullptr ? raw_json : R"({"status":"error","message":"Failed to build response"})";

    std::free(raw_json);
    cJSON_Delete(root);
    return response;
}

std::string build_circuit_response_json(const std::vector<std::string>& subgraph_node_ids,
                                        const CircuitResult& circuit_result, const std::string& timestamp,
                                        bool use_openmp)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddBoolToObject(root, "has_circuit", circuit_result.has_circuit ? 1 : 0);
    cJSON_AddNumberToObject(root, "node_count", static_cast<double>(subgraph_node_ids.size()));
    cJSON_AddNumberToObject(root, "execution_time_ms", circuit_result.execution_time_ms);
    cJSON_AddBoolToObject(root, "use_openmp", use_openmp ? 1 : 0);
    cJSON_AddStringToObject(root, "timestamp", timestamp.c_str());

    cJSON* circuits_json = cJSON_AddArrayToObject(root, "circuits");
    for (const auto& circuit : circuit_result.circuits)
    {
        cJSON* circuit_json = cJSON_CreateArray();
        for (int node_index : circuit)
        {
            cJSON_AddItemToArray(circuit_json,
                                 cJSON_CreateString(subgraph_node_ids.at(static_cast<size_t>(node_index)).c_str()));
        }
        cJSON_AddItemToArray(circuits_json, circuit_json);
    }

    char* raw_json = cJSON_PrintUnformatted(root);
    std::string response =
        raw_json != nullptr ? raw_json : R"({"status":"error","message":"Failed to build response"})";

    std::free(raw_json);
    cJSON_Delete(root);
    return response;
}

} // namespace server
