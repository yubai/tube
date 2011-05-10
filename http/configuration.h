// -*- mode: c++ -*-

#ifndef _CONFIGURATION_H_
#define _CONFIGURATION_H_

#include <yaml-cpp/yaml.h>

#include "http/interface.h"
#include "http/connection.h"

namespace tube {

typedef YAML::Node Node;

class HandlerConfig
{
    HandlerConfig();
    ~HandlerConfig();
public:
    static HandlerConfig& instance() {
        static HandlerConfig ins;
        return ins;
    }

    void register_handler_factory(const BaseHttpHandlerFactory* factory);

    BaseHttpHandler* create_handler_instance(const std::string& name,
                                             const std::string& module);
    BaseHttpHandler* get_handler_instance(const std::string& name) const;

    void load_handlers(const Node& subdoc);
    void load_handler(const Node& subdoc);
private:
    typedef std::map<std::string, const BaseHttpHandlerFactory*> FactoryMap;
    typedef std::map<std::string, BaseHttpHandler*> HandlerMap;
    FactoryMap factories_;
    HandlerMap handlers_;
};

class UrlRuleItemMatcher;

struct UrlRuleItem
{
    typedef std::list<BaseHttpHandler*> HandlerChain;
    HandlerChain handlers;
    UrlRuleItemMatcher* matcher;

    UrlRuleItem(const std::string& type, const Node& subdoc);
    virtual ~UrlRuleItem();
};

class UrlRuleConfig
{
public:
    UrlRuleConfig();
    ~UrlRuleConfig();
    void load_url_rules(const Node& subdoc);
    void load_url_rule(const Node& subdoc);

    const UrlRuleItem* match_uri(HttpRequestData& req_ref) const;

private:
    std::vector<UrlRuleItem> rules_;
};

class VHostConfig
{
    typedef std::map<std::string, UrlRuleConfig> HostMap;
    HostMap host_map_;
    VHostConfig();
    ~VHostConfig();
public:
    static VHostConfig& instance() {
        static VHostConfig ins;
        return ins;
    }

    void load_vhost_rules(const Node& subdoc);
    const UrlRuleItem* match_uri(const std::string& host,
                                 HttpRequestData& req_ref) const;
};

class ThreadPoolConfig
{
    Pipeline& pipeline_;

    ThreadPoolConfig();
    ~ThreadPoolConfig();
public:
    static ThreadPoolConfig& instance() {
        static ThreadPoolConfig ins;
        return ins;
    }

    void load_thread_pool_config(const Node& subdoc);
};

class ServerConfig
{
    Pipeline& pipeline_;
    ServerConfig();
    ~ServerConfig();
public:
    static ServerConfig& instance() {
        static ServerConfig ins;
        return ins;
    }

    std::string config_filename() const { return config_filename_; }
    void set_config_filename(const std::string& val) { config_filename_ = val; }

    void load_static_config();
    void load_config();

    std::string address() const { return address_; }
    std::string port() const { return port_; }
    int listen_queue_size() const { return listen_queue_size_; }

private:
    std::string config_filename_;
    std::string address_;
    std::string port_; // port can be a service, keep it as a string
    int         listen_queue_size_;
};

}

#endif /* _CONFIGURATION_H_ */
