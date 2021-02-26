#include "rapidjson/document.h"
#include <vector>

namespace gdax {
    
    template<typename T>
    struct Parser {
        const T& json;

        Parser(const T& j) : json(j) {}

        template<typename U>
        bool get(const char* name, U& value) const;

        template<>
        bool get<std::string>(const char* name, std::string& value) const {
            if (json.HasMember(name) && json[name].IsString()) {
                value = json[name].GetString();
                return true;
            }
            return false;
        }

        template<>
        bool get<int32_t>(const char* name, int32_t& value) const {
            if (json.HasMember(name)) {
                if (json[name].IsInt()) {
                    value = json[name].GetInt();
                    return true;
                }
                if (json[name].IsUint()) {
                    value = json[name].GetUint();
                    return true;
                }
                if (json[name].IsString()) {
                    value = atoi(json[name].GetString());
                    return true;
                }
            }
            return false;
        }

        template<>
        bool get<uint32_t>(const char* name, uint32_t& value) const {
            if (json.HasMember(name)) {
                if (json[name].IsUint()) {
                    value = json[name].GetUint();
                    return true;
                }
                if (json[name].IsInt()) {
                    value = json[name].GetInt();
                    return true;
                }
                if (json[name].IsString()) {
                    value = atoi(json[name].GetString());
                }
            }
            return false;
        }

        template<>
        bool get<int64_t>(const char* name, int64_t& value) const {
            if (json.HasMember(name)) {
                if (json[name].IsInt64()) {
                    value = json[name].GetInt64();
                    return true;
                }
                if (json[name].IsUint64()) {
                    value = json[name].GetUint64();
                    return true;
                }
                if (json[name].IsString()) {
                    value = _atoi64(json[name].GetString());
                    return true;
                }
            }
            return false;
        }

        template<>
        bool get<uint64_t>(const char* name, uint64_t& value) const {
            if (json.HasMember(name)) {
                if (json[name].IsUint64()) {
                    value = json[name].GetUint64();
                    return true;
                }
                if (json[name].IsInt64()) {
                    value = json[name].GetInt64();
                    return true;
                }
                if (json[name].IsString()) {
                    value = _atoi64(json[name].GetString());
                    return true;
                }
            }
            return false;
        }

        template<>
        bool get<bool>(const char* name, bool& value) const {
            if (json.HasMember(name) && json[name].IsBool()) {
                value = json[name].GetBool();
                return true;
            }
            return false;
        }

        template<>
        bool get<double>(const char* name, double& value) const {
            if (json.HasMember(name)) {
                auto type = json[name].GetType();
                if (json[name].IsNumber()) {
                    value = json[name].GetDouble();
                    return true;
                }
                if (json[name].IsString()) {
                    value = atof(json[name].GetString());
                    return true;
                }
            }
            return false;
        }

        template<>
        bool get<float>(const char* name, float& value) const {
            if (json.HasMember(name)) {
                if (json[name].IsNumber()) {
                    value = json[name].GetFloat();
                    return true;
                }
                if (json[name].IsString()) {
                    value = (float)atof(json[name].GetString());
                    return true;
                }
            }
            return false;
        }

        template<>
        bool get<std::vector<double>>(const char* name, std::vector<double>& value) const {
            if (json.HasMember(name) && json[name].IsArray()) {
                for (auto& item : json[name].GetArray()) {
                    if (item.IsNumber()) {
                        value.push_back(item.GetDouble());
                    }
                }
                return true;
            }
            return false;
        }

        template<>
        bool get<std::vector<float>>(const char* name, std::vector<float>& value) const {
            if (json.HasMember(name) && json[name].IsArray()) {

                for (auto& item : json[name].GetArray()) {
                    if (item.IsNumber()) {
                        value.push_back(item.GetFloat());
                    }
                }
                return true;
            }
            return false;
        }

        template<>
        bool get<std::vector<uint64_t>>(const char* name, std::vector<uint64_t>& value) const {
            if (json.HasMember(name) && json[name].IsArray()) {

                for (auto& item : json[name].GetArray()) {
                    if (item.IsNumber()) {
                        value.push_back(item.GetUint64());
                    }
                }
                return true;
            }
            return false;
        }

        template<typename U>
        U get(const char* name) const {
            if (json.HasMember(name) && json[name].IsString()) {
                return json[name].GetString();
            }
            return "";
        }
    };
}
