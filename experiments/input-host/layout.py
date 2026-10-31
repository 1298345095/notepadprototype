"""Compare the geometry of a shaped line with added single-code-point advances."""
import ctypes as c
from ctypes.util import find_library

pango = c.CDLL(find_library('pango-1.0'))
cairo = c.CDLL(find_library('pangocairo-1.0'))
obj = c.CDLL(find_library('gobject-2.0'))
ptr, integer = c.c_void_p, c.c_int

class Rectangle(c.Structure):
    _fields_ = [(name, integer) for name in ('x', 'y', 'width', 'height')]

def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function

version = bind(pango, 'pango_version_string', c.c_char_p)
font_map = bind(cairo, 'pango_cairo_font_map_get_default', ptr)()
context = bind(pango, 'pango_font_map_create_context', ptr, ptr)(font_map)
layout = bind(pango, 'pango_layout_new', ptr, ptr)(context)
font = bind(pango, 'pango_font_description_from_string', ptr, c.c_char_p)(b'DejaVu Sans Mono 12')
bind(pango, 'pango_layout_set_font_description', None, ptr, ptr)(layout, font)
set_text = bind(pango, 'pango_layout_set_text', None, ptr, c.c_char_p, integer)
cursor = bind(pango, 'pango_layout_get_cursor_pos', None, ptr, integer, c.POINTER(Rectangle), c.POINTER(Rectangle))
size = bind(pango, 'pango_layout_get_size', None, ptr, c.POINTER(integer), c.POINTER(integer))

def single_width(ch):
    data = ch.encode()
    set_text(layout, data, len(data))
    width = integer()
    size(layout, c.byref(width), None)
    return width.value

print('Pango', version().decode(), 'DejaVu Sans Mono 12')
for prefix in ('A中😀', 'Ae\u0301'):
    data = (prefix + 'B').encode()
    set_text(layout, data, len(data))
    caret = Rectangle()
    cursor(layout, len(prefix.encode()), c.byref(caret), None)
    total = sum(single_width(ch) for ch in prefix)
    print(repr(prefix + 'B'), 'caret', caret.x / 1024, 'sum', total / 1024)

bind(pango, 'pango_font_description_free', None, ptr)(font)
unref = bind(obj, 'g_object_unref', None, ptr)
unref(layout)
unref(context)
