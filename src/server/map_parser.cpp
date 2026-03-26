#include "map_parser.hpp"
#include <cJSON.h>
#include <stdexcept>
#include <cstring>

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

        // Filter rules
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

} // namespace server
