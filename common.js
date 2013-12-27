var safe_common = {};

(function (exports) {
    exports.SVG_NS_URI = 'http://www.w3.org/2000/svg';

    exports.RuntimeException = function (message) {
        this.message = message;
        this.name = "UserException";
    };

    exports.assert = function (bool, msg) {
        if (bool) return;
        alert(msg);
    };

    var set_opacity = function (elt, opacity) {
        elt.setAttribute('opacity', opacity);
        elt.style.opacity = opacity;
    };

    var dpi = null;
    exports.get_dpi = function () {
        if (dpi !== null) return dpi;
        var _rect = document.createElementNS(safe_common.SVG_NS_URI, 'rect');
        _rect.setAttribute("width", "1in");
        _rect.setAttribute("height", "1in");
        dpi = _rect.width.baseVal.value;
        return dpi;
    };

    exports.remove_element = function (elt) {
        return elt.parentNode.removeChild(elt);
    };
    
    var prefix_all_ids = function (root_elt, id_prefix) {
        var nodes = [root_elt];
        while (nodes.length) {
            var curnode = nodes.pop();
            var cur_id = curnode.id;
            if (cur_id) curnode.setAttribute("id", id_prefix + "-" + cur_id);
            for (var i = 0; i < curnode.childNodes.length; ++i) {
                if (curnode.childNodes[i].nodeType == 1) { // element node
                    nodes.push(curnode.childNodes[i]);
                }
            }
        }
    };

    var deep_clone_element = function (source_element) {
        var deep = true;
        // TODO: support this on older browsers that don't support deep argumentn
        var clone = source_element.cloneNode(deep);
        return clone;
    };

    var namespace_uri = function (element) {
        return element.lookupNamespaceURI(element.prefix);
    };

    var is_id_attribute = function (element) {
        return (element.localName == "id"  &&
                (element.prefix == "xml" || !element.prefix));
    };

    exports.array = function (sequenceable) {
        var toret = [];
        for (var i = 0; i < sequenceable.length; ++i) {
            toret.push(sequenceable[i]);
        }
        return toret;
    };

    exports.has_class = function (element, cls) {
        return (' ' + element.className + ' ').indexOf(' ' + cls + ' ') > -1;
    };

    exports.svg_instantiate_all_uses = function () {
        var uses = safe_common.array(document.getElementsByTagNameNS(safe_common.SVG_NS_URI, 'use'));
        for (var i = 0; i < uses.length; ++i) {
            var use_element = uses[i];
            var other_id = use_element.getAttributeNS('http://www.w3.org/1999/xlink', 'href');
            if (other_id[0] != '#') {
                throw new safe_common.RuntimeException("only id hrefs are supported!");
            }

            var source_element = document.getElementById(other_id.substr(1));
            if (!source_element) {
                throw new safe_common.RuntimeException("no source element!");
            }

            if (source_element.tagName == "svg" ||
                source_element.tagName == "symbol" ||
                source_element.namespaceURI != safe_common.SVG_NS_URI) {
                throw new safe_common.RuntimeException("we only support g sources");
            }

            var new_node = deep_clone_element(source_element, id_prefix);

            var id_prefix = use_element.getAttribute("id");
            if (!id_prefix) {
                throw new safe_common.RuntimeException("use element must have id!");
            }
            prefix_all_ids(new_node, id_prefix);

            // TODO: we're really sloppy here when it comes to XML attribute manipulation
            // this is in no means fully general
            // we should always use the correct xml namespaces when inserting into
            // a different part of the dom tree (since new_node comes from source_element)
            use_element.parentNode.insertBefore(new_node, use_element);

            // transfer all attributes from use node to the cloned node
            for (var j = 0; j < use_element.attributes.length; ++j) {
                var attr = use_element.attributes[j];
                if (((attr.nodeName == "x" ||
                      attr.nodeName == "y" ||
                      attr.nodeName == "width" ||
                      attr.nodeName == "height") &&
                     namespace_uri(attr) == safe_common.SVG_NS_URI) ||
                    is_id_attribute(attr)) {
                    continue;
                }

                // it's fine to use nodeName verbatim since the cloned element
                // and the use element are at the same point in the xml hierarchy
                new_node.setAttribute(attr.nodeName, attr.nodeValue);
            }

            // now transfer position attributes according to use element rules
            // it doesn't look like chrome considers <svg:foo transform="bar">
            //   svg_foo_elt.getAttributeNS(SVG_NS_URI, "transform") the same as
            //   svg_foo_elt.getAttribute("transform")
            // so we do it according to prefix
            var transform_attribute = "transform";
            var svg_prefix = new_node.lookupPrefix(safe_common.SVG_NS_URI);
            if (svg_prefix) transform_attribute = svg_prefix + ":" + transform_attribute;

            var existing_transform_string = new_node.getAttribute(transform_attribute);
            if (!existing_transform_string) existing_transform_string = "";
            existing_transform_string += (" translate(" +
                                          use_element.getAttribute("x") + "," +
                                          use_element.getAttribute("y") + ")");
            new_node.setAttribute(transform_attribute, existing_transform_string);

            safe_common.remove_element(use_element);
        }
    };

    exports.set_up_button = function (id_prefix) {
        var BUTTON_HILITE_ID = 'button_hover_gradient';
        var BUTTON_CLICK_HILITE_ID = 'button_click_mask';
        var BUTTON_HIT_REGION_ID = 'button_hit_region';

        var mouse_entered = false;
        var mouse_pressed = false;
        
        var set_button_style = function () {
            if (mouse_pressed) {
                set_opacity(document.getElementById(id_prefix + BUTTON_CLICK_HILITE_ID), '1');
                set_opacity(document.getElementById(id_prefix + BUTTON_HILITE_ID), '0');
            }
            else if (mouse_entered) {
                set_opacity(document.getElementById(id_prefix + BUTTON_CLICK_HILITE_ID), '0');
                set_opacity(document.getElementById(id_prefix + BUTTON_HILITE_ID), '1');
            }
            else {
                set_opacity(document.getElementById(id_prefix + BUTTON_CLICK_HILITE_ID), '0');
                set_opacity(document.getElementById(id_prefix + BUTTON_HILITE_ID), '0');
            }
        };
        
        var webmock_button_on_hover = function (evt) {
            mouse_entered = true;
            set_button_style();
        };
        
        var webmock_button_on_hoverout = function (evt) {
            mouse_entered = false;
            set_button_style();
        };
        
        var webmock_button_on_mousedown = function (evt) {
            mouse_pressed = true;
            set_button_style();
            return false;
        };

        var highlight_elt = document.getElementById(id_prefix + BUTTON_HILITE_ID);
        var hit_region_elt = document.getElementById(id_prefix + BUTTON_HIT_REGION_ID);

        set_button_style();
        
        hit_region_elt.onmouseover = webmock_button_on_hover;
        hit_region_elt.onmouseout = webmock_button_on_hoverout;
        hit_region_elt.onmousedown = webmock_button_on_mousedown;

        var global_mouse_up = function (evt) {
            if (mouse_pressed && mouse_entered) {
                /* TODO: click action to be parsed from ui desc */
            }
            mouse_pressed = false;
            set_button_style();
        };

        // XXX: use different function for IE
        document.rootElement.addEventListener('mouseup', global_mouse_up);

        // TODO: this is pretty hacky
        var title = document.getElementById(id_prefix + 'button').getAttribute("safe_button:label")
        document.getElementById(id_prefix + 'tspan5753').textContent = title;
        var text_width_px = document.getElementById(id_prefix + 'button_text').getBBox().width;
        var button_width_px = highlight_elt.getBBox().width;
        var offset = ((button_width_px - text_width_px) / 2 +
                      parseFloat(highlight_elt.getAttribute("x")));
        document.getElementById(id_prefix + 'tspan5753').setAttribute("x", offset);
    }

    exports.inkscape_move_main_content = function () {
        var root_element = document.rootElement;

        var content_svg_elt = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        var all_root_children = safe_common.array(document.rootElement.childNodes);
        for (var i = 0; i < all_root_children.length; ++i) {
            var elt = all_root_children[i];
            if (elt.nodeType == 1 && elt.hasAttribute("safe_common:main_content")) {
                content_svg_elt.appendChild(safe_common.remove_element(elt));
            }
        }

        original_content_width = parseFloat(root_element.getAttribute("width"));
        original_content_height = parseFloat(root_element.getAttribute("height"));

        content_svg_elt.setAttribute("version", "1.1");
        content_svg_elt.setAttribute("viewBox", 
                                     ("0 0 " +
                                      original_content_width + " " +
                                      original_content_height));

        root_element.appendChild(content_svg_elt);

        return [content_svg_elt, original_content_height / original_content_width];
    };
})(safe_common);
