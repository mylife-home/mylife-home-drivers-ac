
struct admin_attr attr_value = {
  .name = "value",
  .writable = 1
};

struct admin_attr* attrs[] = {
  &attr_value,
  NULL
};

struct admin_def def = {
  .class = "ac_dimmer",
  .object_prefix = "dimmer",
  .attrs = attrs
};