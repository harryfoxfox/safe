var safe_splash = {};

(function (exports) {
    var TEXT_ID = 'text5874';
    var LOGO_ID = 'g3540';
    var TAGLINE_ID = 'text5743';
    var BUTTONS_ID = 'g3559';

    var TEXT_MARGIN_PX = 8;
    var MIN_CONTENT_HEIGHT_IN = 6;

    var LOGO_DELAY = 0.4;
    var LOGO_DURATION = 1.0;
    var TAGLINE_DELAY = 0.5;
    var TAGLINE_DURATION = 1;
    var BUTTON_DELAY = 0.25;
    var BUTTON_DURATION = 1.0;

    var content_root;
    var content_aspect_ratio;

    var footer_height = function () {
        return 10 * safe_common.get_dpi() / 72;
    };

    var webmock_on_resize = function () {
        // reposition all elements in this function
        var main_viewport_width_px = window.innerWidth;
        var main_viewport_height_px = window.innerHeight;

        // first find out what is bounding the content
        var content_width_px = main_viewport_width_px;
        // we want the content to scale to the viewport except when 
        // height constrained and the height is smaller than MIN_CONTENT_HEIGHT_IN
        var content_height_px = Math.min(Math.max(main_viewport_height_px - footer_height(),
                                                  MIN_CONTENT_HEIGHT_IN * safe_common.get_dpi()),
                                         content_width_px * content_aspect_ratio);

        var new_height_px = Math.max(window.innerHeight,
                                     content_height_px + footer_height());

        // reposition root element
        var root_element = document.rootElement;
        root_element.setAttribute("width", main_viewport_width_px);
        root_element.setAttribute("height", new_height_px);

        // reposition main content
        content_root.setAttribute("width", content_width_px);
        content_root.setAttribute("height", content_height_px);

        // reposition footer
        var text_element = document.getElementById(TEXT_ID);
        text_element.setAttribute("x", TEXT_MARGIN_PX);
        text_element.setAttribute("y", new_height_px - TEXT_MARGIN_PX);
    };

    var webmock_on_load = function () {
        safe_common.svg_instantiate_all_uses();
        
        var root_element = document.rootElement;

        var _ret = safe_common.inkscape_move_main_content();
        content_root = _ret[0];
        content_root.setAttribute("preserveAspectRatio", "xMidYMin");
        content_aspect_ratio = _ret[1];

        // set font size
        var text_element = document.getElementById(TEXT_ID);
        text_element.style['font-size'] = footer_height() + 'px';

        // set up buttons
        var button_defs = ["", "learn_more_button-"];
        for (var i = 0; i < button_defs.length; ++i) {
            safe_common.set_up_button(button_defs[i]);
        }

        // finally kick off a resize
        webmock_on_resize();

        window.onresize = webmock_on_resize;

        // now start animations
        var animate_in_buttons = function () {
            var cb = function (fraction_done) {
                safe_common.set_opacity(document.getElementById(BUTTONS_ID), fraction_done);
            };

            safe_common.linear_animate(BUTTON_DURATION, cb);
        };

        var animate_in_tagline = function () {
            var cb = function (fraction_done) {
                safe_common.set_opacity(document.getElementById(TAGLINE_ID), fraction_done);
            };

            safe_common.linear_animate(TAGLINE_DURATION, cb,
                                       function () {
                                           setTimeout(animate_in_buttons, BUTTON_DELAY * 1000);
                                       });
        };

        var animate_in_logo = function () {
            var cb = function (fraction_done) {
                safe_common.set_opacity(document.getElementById(LOGO_ID), fraction_done);
            };
            safe_common.linear_animate(LOGO_DURATION, cb,
                                       function () {
                                           setTimeout(animate_in_tagline, TAGLINE_DELAY * 1000);
                                       });
        };

        var _ids = [LOGO_ID, TAGLINE_ID, BUTTONS_ID];
        for (var i = 0; i < _ids.length; ++i) {
            safe_common.set_opacity(document.getElementById(_ids[i]), 0);
        }

        setTimeout(animate_in_logo, LOGO_DELAY * 1000);
    };
    
    window.onload = webmock_on_load;
})(safe_splash);
