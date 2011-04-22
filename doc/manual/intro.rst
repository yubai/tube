Introduction
============

.. highlight:: none

Overview
--------

Tube Web Server is a fast synchronous server using event notification and thread pools.  It was intented for easily extend using external modules as well as fair performance and scalability.  Therefore, it uses synchronous design instead of asynchronous (which `nginx`_ does), because we believe that writing synchrouous module is easier and more natural.

Tube Web Server employed a pipeline based architecture. It has several thread pools to transform asynchronous IO events into messages.  External module therefore would be able to process those messages in a thread pool using block system calls.  External modules are primarily designed for custom HTTP handler, and user can configure those handlers (custom and default) based on their own demands.

Unlike `Apache`_ and `nginx`_ uses their own configuration language, Tube Web Server uses yaml to configure.  This will make the configuration file easy to read and maintain.  The configuration file specification is descibed in the :doc:`conf` chapter.

.. _`Apache` : http://httpd.apache.org/
.. _`nginx`: http://nginx.net/

Build and Installation
----------------------

Tube Web Server is not officially released yet, therefore, the only way you get our source is through mainline repository.  The version control repository is hosted at github.  You can get the source code through: ::

    git clone git://github.com/mikeandmore/tube.git

This is the mainline repository, it updates very frequently since Tube Web Server is under heavy development.  Therefore, it is **NOT** guaranteed to work.

Tube Web Server is written in C/C++. You need to have a C++ compiler to build Tube Web Server. Besides that, you'll also need the following tools and libraries installed.
 
* ``SCons`` > 2.0. available at `<http://www.scons.org/>`_
* boost library with the following component:

  * ``boost::thread``
  * ``boost::function``
  * ``boost::smart_ptr``
  * ``boost::xpressive``

* ``yaml-cpp``
* ``ragel`` available at `<http://www.complang.org/ragel/>`_

To Build Tube, simply run ::

    % scons 

If you want the release version (which doesn't have log and debug symbol) ::
    
    % scons release=1
    
If the building process succeed, there will be executables files under ``build/`` directory including ``libtube.so``, ``libtube-web.so`` and ``tube-server``. After building process succeeded, run the following to install ::

    # scons install

If you want to install in a sandbox directory (or DESTDIR in automake terminology), use ::

    % scons install --install-sandbox=<dir>

After installation, you might want to try: ::

    % tube-server -h

To see if linking of the binaries succeeded or not.
