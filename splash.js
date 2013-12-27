var safe_splash = {};

(function (exports) {
    var SVG_NS_URI = 'http://www.w3.org/2000/svg';

    var CONTENT_ID = 'content';
    var NEW_VIEWPORT_ID = 'new_viewport';
    var TEXT_ID = 'text5874';

    var TEXT_MARGIN_PX = 8;
    var MIN_CONTENT_HEIGHT_IN = 6;

    var original_content_height;
    var original_content_width;

    var dpi;

    var footer_height = function () {
        return 10 * dpi / 72;
    };

    var webmock_on_resize = function () {
        // reposition all elements in this function
        var main_viewport_width_px = window.innerWidth;
        var main_viewport_height_px = window.innerHeight;

        // first find out what is bounding the content
        var content_aspect_ratio = original_content_height / original_content_width;
        var content_width_px = main_viewport_width_px;
        // we want the content to scale to the viewport except when 
        // height constrained and the height is smaller than MIN_CONTENT_HEIGHT_IN
        var content_height_px = Math.min(Math.max(main_viewport_height_px - footer_height(),
                                                  MIN_CONTENT_HEIGHT_IN * dpi),
                                         content_width_px * content_aspect_ratio);

        var new_height_px = Math.max(window.innerHeight,
                                     content_height_px + footer_height());

        // reposition root element
        var root_element = document.rootElement;
        root_element.setAttribute("width", main_viewport_width_px);
        root_element.setAttribute("height", new_height_px);

        // reposition main content
        var content_root = document.getElementById(NEW_VIEWPORT_ID);
        content_root.setAttribute("width", content_width_px);
        content_root.setAttribute("height", content_height_px);

        // reposition footer
        var text_element = document.getElementById(TEXT_ID);
        text_element.setAttribute("x", TEXT_MARGIN_PX);
        text_element.setAttribute("y", new_height_px - TEXT_MARGIN_PX);
    };

    var set_opacity = function (elt, opacity) {
        elt.setAttribute('opacity', opacity);
        elt.style.opacity = opacity;
    }

    var compute_dpi = function () {
        var _rect = document.createElementNS(SVG_NS_URI, 'rect');
        _rect.setAttribute("width", "1in");
        _rect.setAttribute("height", "1in");
        return _rect.width.baseVal.value;
    };

    var remove_element = function (elt) {
        elt.parentElement.removeChild(elt);
    };
    
    var prefix_all_ids = function (root_elt, id_prefix) {
        var nodes = [root_elt];
        while (nodes.length) {
            var curnode = nodes.pop();
            var cur_id = curnode.getAttribute("id");
            if (cur_id) curnode.setAttribute("id", id_prefix + "-" + cur_id);
            for (var i = 0; i < curnode.children.length; ++i) {
                nodes.push(curnode.children[i]);
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

    var array = function (sequenceable) {
        var toret = [];
        for (var i = 0; i < sequenceable.length; ++i) {
            toret.push(sequenceable[i]);
        }
        return toret;
    };

    var instantiate_all_uses = function () {
        var uses = array(document.getElementsByTagNameNS(SVG_NS_URI, 'use'));
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
                source_element.namespaceURI != SVG_NS_URI) {
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
            use_element.parentElement.insertBefore(new_node, use_element);

            // transfer all attributes from use node to the cloned node
            for (var j = 0; j < use_element.attributes.length; ++j) {
                var attr = use_element.attributes[j];
                if (((attr.nodeName == "x" ||
                      attr.nodeName == "y" ||
                      attr.nodeName == "width" ||
                      attr.nodeName == "height") &&
                     namespace_uri(attr) == SVG_NS_URI) ||
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
            var svg_prefix = new_node.lookupPrefix(SVG_NS_URI);
            if (svg_prefix) transform_attribute = svg_prefix + ":" + transform_attribute;

            var existing_transform_string = new_node.getAttribute(transform_attribute);
            if (!existing_transform_string) existing_transform_string = "";
            existing_transform_string += (" translate(" +
                                          use_element.getAttribute("x") + "," +
                                          use_element.getAttribute("y") + ")");
            new_node.setAttribute(transform_attribute, existing_transform_string);

            remove_element(use_element);
        }
    };

    var set_up_button = function (id_prefix) {
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
        var text_width_px = document.getElementById(id_prefix + 'button_text').clientWidth;
        var button_width_px = highlight_elt.width.baseVal.value;
        var offset = ((button_width_px - text_width_px) / 2 +
                      parseFloat(highlight_elt.getAttribute("x")));
        document.getElementById(id_prefix + 'tspan5753').setAttribute("x", offset);
    }

    var webmock_on_load = function () {
        instantiate_all_uses();
        
        var root_element = document.rootElement;

        // first compute dpi, we use a little hack
        dpi = compute_dpi();

        var content_svg_elt = document.createElementNS('http://www.w3.org/2000/svg', 'svg');

        // move main content layer into it's own svg tag
        var content_elt = document.getElementById(CONTENT_ID);
        remove_element(content_elt);
        content_svg_elt.appendChild(content_elt);

        root_element.appendChild(content_svg_elt);

        original_content_width = parseFloat(root_element.getAttribute("width"));
        original_content_height = parseFloat(root_element.getAttribute("height"));

        content_svg_elt.setAttribute("id", NEW_VIEWPORT_ID);
        content_svg_elt.setAttribute("version", "1.1");
        content_svg_elt.setAttribute("viewBox", 
                                     ("0 0 " +
                                      original_content_width + " " +
                                      original_content_height));
        content_svg_elt.setAttribute("preserveAspectRatio", "xMidYMin");

        // set font size
        var text_element = document.getElementById(TEXT_ID);
        text_element.style['font-size'] = footer_height() + 'px';

        // set up buttons
        var button_defs = ["", "learn_more_button-"];
        for (var i = 0; i < button_defs.length; ++i) {
            set_up_button(button_defs[i]);
        }

        // finally kick off a resize
        webmock_on_resize();
    };
    
    window.onload = webmock_on_load;
    window.onresize = webmock_on_resize;
})(safe_splash);
