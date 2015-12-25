
struct admin_attr attr_value = {
  .name = "value",
  .writable = 0
};

struct admin_attr* attrs[] = {
  &attr_value,
  NULL
};

struct admin_def def = {
  .class = "ac_button",
  .object_prefix = "button",
  .attrs = attrs
};