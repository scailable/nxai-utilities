void *nxai_load_library( const char *filename );

char *nxai_get_error( void );

void *nxai_load_library_with_namespace( const char *filename, int nsid );

void *nxai_get_library_symbol( void *handle, const char *symbol );

void nxai_free_library( void *handle );
