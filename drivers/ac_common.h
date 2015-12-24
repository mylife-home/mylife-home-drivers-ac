#ifndef __MYLIFE_AC_COMMON_H__
#define __MYLIFE_AC_COMMON_H__

// Redefinition of macro to enable permissions to world
#define __ATTR_NOCHECK(_name, _mode, _show, _store) {                   \
        .attr = {.name = __stringify(_name),                            \
                 .mode =_mode },                                        \
        .show   = _show,                                                \
        .store  = _store,                                               \
}

#define DEVICE_ATTR_NOCHECK(_name, _mode, _show, _store) \
        struct device_attribute dev_attr_##_name = __ATTR_NOCHECK(_name, _mode, _show, _store)

#endif // __MYLIFE_AC_COMMON_H__
