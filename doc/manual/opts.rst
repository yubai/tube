Command Line Options and Switches
=================================

Some of the features are turned on and off through command line arguments.  These arguments are need to be loaded before any modules or configuration applied. To show all of the options and switches you can run ::

    % tube-server -h    
    Usage: build/tube-server -c config_file [ -m module_path -u uid ]
     
      -c             Specify the configuration file. Required.
      -m             Specify the module path. Optional.
      -u             Set uid before server starts. Optional.
      -h             Help

Configuration File Path
-----------------------

``-c config_file`` options is used to pass the configuration file.  Each server process can only accept one configuration file.  The configuration file is in YAML format, if it's not readable or its format incorrect, server will exit with an error exit status.  

For further information about YAML syntax, please refer to `<http://www.yaml.org/>`_.  For full specification of the configuration file, you can turn to the next chapter :doc:`conf`


This option is always required. If not specified, server won't start.

Module Path
-----------

``-m module_path`` option is used to pass the directory that stores all enabled module.  Server will load all valid ``.so`` file under that directory with no exception.  Therefore, it is recommended use the directory that containing soft-links to modules.

If the module you specified doesn't exist, or for some reasons, cannot access by server, server won't load any module and startup silently rather than crash or exit.

This option is optional, if you didn't pass this argument, server won't load any external module either.

Change UID
----------

``-u uid`` option is used for switching uid before server starts.  This is primarily for security concern.

In order to use ``-u`` option, you need to make sure that after switching the uid, server process still have access to read the configuration file as well as module directory (if necessary).

This option is optional, if you didn't pass this argument, server will run as the current user by default.
