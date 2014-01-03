var safe_download = {};

(function (exports) {
    var TEXT_ID = 'text5874';

    var TEXT_MARGIN_PX = 8;
    var MIN_CONTENT_HEIGHT_IN = 6;

    var content_root;
    var content_aspect_ratio;

    var footer_height = function () {
        return 10 * safe_common.get_dpi() / 72;
    };

    var onresize = function () {
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

    window.onload = function () {
        safe_common.svg_instantiate_all_uses();

        var _ret = safe_common.inkscape_move_main_content();
        content_root = _ret[0];
        content_root.setAttribute("preserveAspectRatio", "xMidYMin");
        content_aspect_ratio = _ret[1];

        // set font size
        var text_element = document.getElementById(TEXT_ID);
        text_element.style['font-size'] = footer_height() + 'px';

        window.onresize = onresize;

        onresize();
    };
})(safe_download);
