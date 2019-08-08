#ifndef __LANG_PRIVATE
#define __LANG_PRIVATE
ANN m_bool import_prim(const Gwi gwi);
ANN m_bool import_complex(const Gwi gwi);
ANN m_bool import_vec3(const Gwi gwi);
ANN m_bool import_vec4(const Gwi gwi);
ANN m_bool import_object(const Gwi gwi);
ANN m_bool import_vararg(const Gwi gwi);
ANN m_bool import_string(const Gwi gwi);
ANN m_bool import_shred(const Gwi gwi);
ANN m_bool import_event(const Gwi gwi);
ANN m_bool import_ugen(const Gwi gwi);
ANN m_bool import_array(const Gwi gwi);
ANN m_bool import_ptr(const Gwi gwi);
ANN m_bool import_func(const Gwi gwi);
ANN m_bool import_modules(const Gwi gwi);
ANN m_bool import_tuple(const Gwi gwi);
#endif
