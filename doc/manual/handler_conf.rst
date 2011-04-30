Handler Specific Configuration
==============================

In this chapter, we'll cover the handler options provided build-in or external modules.

Static Handler
--------------

Static handler's module name is ``static``.  It's primarily used for servering static content.

doc_root
````````

**Required**

Document root path.  Static handler will lookup files and directories in under this directory.  Tube won't do ``chroot`` to the directory you specified.

error_root
``````````

Root path for error pages.  Error pages under that directory should be named as ``error_code.html``. ``error_code`` would be like ``404`` or ``500``.  For example: ``404.html``

allow_index
```````````

``true`` or ``false``, whether allow list directory content.

index_page_css
``````````````

CSS url for directory list page.  This however is *NOT* the file path on the server filesystem.  This is a url that embeded into the directory list page.  Therefore, you have to make sure that this url is accessable.

max_cache_entry
```````````````

Maximum IO cache entry, set to 0 to disable IO cache.

max_entry_size
``````````````

Size of each cache entry.  Tube would only cache the file smaller than this size.  Larger files are not beening cached, they're send through :manpage:`sendfile(2)` instead.
