Server Configuration
====================

YAML Syntax
-----------

Tube's configuration format is written in YAML.  YAML's syntax is straightforward and easy to follow.  To learn YAML please refer to `YAML Website <http://www.yaml.org/start.html>`_.  There is tutorial written by Xavier Shay, please refer to `this article <http://rhnh.net/2011/01/31/yaml-tutorial>`_.

The syntax most commonly used in Tube's configuration file is array, key map.  So as long as you can understand these two YAML syntax elements, you should be ready to modify Tube's configuration file.

Straightforward Sample
----------------------


Tube's configuration file is very easy to understand if you know YAML.  Here, we give a simple sample configuration file for tutorial.  If you cannot understand what's going on here, you might want to refer to the next sections.

.. code-block:: yaml

    address: 0.0.0.0
    port: 8000
    read_stage_pool_size: 2
    parser_stage_pool_size: 2
    write_stage_pool_size: 4
    handler_stage_pool_size: 4
    listen_queue_size: 32
    recycle_threshold: 16
    idle_timeout: 15
    
    # modules and handlers
    handlers:
      - name: default
        module: static
        doc_root: /mnt/data/picture     # doc root
        error_root: data/pages/error    # error page
        index_page_css: /index_theme/plain.css # style of directory list
        max_cache_entry: 1024           # cache related
        max_entry_size: 16384
      - name: theme_static
        module: static
        doc_root: data/style
        allow_index: false              # don't allow index
        max_cache_entry: 64
        max_entry_size: 16384

    # virtual host settings
    host:
      - domain: default
        url-rules:
          - type: prefix                # url matching algorithm
            prefix: /index_theme
            chain:                      # handler chains
              - theme_static
          - type: none
            chain:
              - default

Basic Configuration Parameter
-----------------------------

In this section, we'll introduce some of basic configuration parameters.  Those parameters will affect server's basic behavior and functionality.

address
```````
**Required**

The listening address.  If you want to Tube accepting *any* connection, you can set it to ``0.0.0.0``. This option cannot be given to a domain, it has to be an IP address.

port
````

 **Required**

The port number. You have to make sure that this port number is not beening used by other application.

idle_time
`````````

The maximum time that a client connection can idle.  If the client connection doesn't send any data for a long time, server will close the connection.  This is the maximum time server can tolerent idle connection.

If set to zero, meaning disable idle connection scan, which is dangerous. Default value is 15 seconds.

This option has a performance impact.  If it's too small, server will frequently scan for idle connection, therefore affects the performance.  On the other hand, if it's too large, idle connection might use up all the file descriptors.

read_stage_pool_size, write_stage_pool_size, handler_stage_pool_size and parser_stage_pool_size
```````````````````````````````````````````````````````````````````````````````````````````````

Thread pool size for each stage.  

``read_stage_pool_size`` is used for reading data from client connection.  Since reading stage employed a event poll, so the thread pool size can be as small as number of cores of the CPU.

``write_stage_pool_size`` is used for sending buffer or file to client connection.  Tube employed a blocking writing scheme, therefore this value could be larger.  Mostly is could be 1-2 times larger than number of cores of the CPU.

``handler_stage_pool_size`` is the thread pool used for HTTP handlers, this parameter should be tuned according to server loads and demands.

``parser_stage_pool_size`` is the thread pool used for parsing HTTP protocols, this parameter should be small, since parsing HTTP protocol is CPU bounded.

Thread pool size is a sensitive performance tuning parameter, we will give some of advices in the :doc:`perf` chapter.

Virtual Host Configuration
--------------------------

Tube is able to configure to support multiple virtual hosts, using the ``host`` key.  The ``host`` key indicates a array, each item of array indicates a virtual host configuration including url matching and the handler-chain.  Each ``host`` configuration must have a ``default`` domain to support, otherwise Tube don't know which domain to fallback when host header match none of current virtual hosts.

For a intuitive sample, ``host`` configuration looks like this.

.. code-block:: yaml

    # virtual host settings
    host:
      - domain: default
        url-rules:
          - type: prefix                # url matching algorithm
            prefix: /index_theme
            chain:                      # handler chains
              - theme_static
          - type: none
            chain:
              - default

domain
``````

**Required**

The domain name of the virtual host.  By default, the ``default`` domain matches every domain name.

url-rules
`````````

**Required**

An array, specified url matching rules for this virtual host, therefore Tube is able to handle requests according to url using different handlers.  Each of the element in the array is a matching rule.  Matching result is the first matching rule that matches the request url.

type
````

**Required**

Define the matching algorithm of the rule. Currently, ``prefix``, ``regex`` and ``none`` is supported. 

* ``prefix``: Prefix match.  However, prefix rule will erase the matching prefix.  For example. If url is ``/hello/world`` and the prefix for match is ``/hello``, then it match, but url will changed into ``/world``.
* ``regex``: Regular expression match.  Tube is using ``boost::xpressive`` to support regular expression.
* ``none``: Matches everything. It won't change the url either.

chain
`````

**Required**

specified all the handlers to serve this url would be triggered.  It's also an array, each element is the name of the handler.  For handler specification, please refer to the `Handler Configuration`_.


Handler Configuration
---------------------

Handler configuration basically gathered all handlers together. 

.. code-block:: yaml

    # modules and handlers
    handlers:
      - name: default
        module: static
        doc_root: /mnt/data/picture     # doc root
        error_root: data/pages/error    # error page
        index_page_css: /index_theme/plain.css # style of directory list
        max_cache_entry: 1024           # cache related
        max_entry_size: 16384
      - name: theme_static
        module: static
        doc_root: data/style
        allow_index: false              # don't allow index
        max_cache_entry: 64
        max_entry_size: 16384

to start with, it have to be all under the ``handlers`` key, which contains an array. Each elements inside the array is a module configuration, which is a handler instance.

name
````

**Required**

Name of the handler instance.  Name with a ``default`` is recommended but not required.

module
``````

**Required**

Specify the handler module.  This defined what kinds of handler this handler might be.  Different handler module will have different options, please refer to the following section on :doc:`handler_conf`.

Beside modules that are build-in with Tube, Tube is also able to load external modules by specify module load path on the command line, please refer to :doc:`opts` for further detail.
