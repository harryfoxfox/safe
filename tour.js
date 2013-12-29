var safe_tour = {};

(function (exports) {
    var SLIDE_LAYER_IDS = ["layer5", "g6867", "g6972", "layer2", "layer6",
                           "g7076", "g7086", "g7098", "g7108", "g7118",
                           "layer8", "g7158", "g7176", "g7194", "g7212",
                           "g7230"];
    var MAIN_LAYER_ID = "layer1";
    var TOUR_BUTTON_ID = 'tour_button_id';
    var TIMER_LINK_ID = 'timer_link_id';
    var TIMER_PATH_ID = "timerpath";
    var ARROW_ID = "path4743";
    var ARROW_GROUP_ID = "arrow_group_id";

    var BUTTON_WIDTH_INCHES = 1.5;
    var ARROW_WIDTH_INCHES = 0.75;
    var TIMER_SIZE_INCHES = 0.5;
    var BUTTON_MARGIN_PX = 8;
    var SLIDE_DURATION_SECS = 5;
    var TIMER_UPDATE_FREQUENCY = 30;
    var ARROW_BLINK_FREQUENCY = 5;

    var start_time = 0;
    var timer_update_id = null;

    var cur_slide;

    var reparent_node = function (node, ns_uri, tag_name) {
        var orig_parent = node.parentNode;
        var new_node = document.createElementNS(ns_uri, tag_name);
        new_node.appendChild(safe_common.remove_element(node));
        orig_parent.appendChild(new_node);
        return new_node;
    };

    var reset_timer = function () {
        start_time = (new Date()).getTime();
        timer_widget_set_percentage(0);
        if (timer_update_id === null) {
            var timer_func = function () {
                var cur_time = (new Date()).getTime();
                var timer_percentage = (cur_time - start_time) / 1000 / SLIDE_DURATION_SECS;
                timer_widget_set_percentage(timer_percentage);

                if (timer_percentage >= 1.0) {
                    cur_slide += 1;
                    move_to_slide(cur_slide);
                }
            };
            timer_update_id = setInterval(timer_func, 1000 / TIMER_UPDATE_FREQUENCY);
        }
    };

    var stop_timer = function () {
        // disable timer and show arrow
        if (timer_update_id !== null) {
            clearInterval(timer_update_id);
            timer_update_id = null;
        }
    }

    var timer_widget_set_link = function (next_slide) {
        var link_tag = document.getElementById(TIMER_LINK_ID);
        link_tag.onclick = function () {
            move_to_slide(next_slide);
            return false;
        };
        link_tag.setAttributeNS(safe_common.XLINK_NS_URI, "href", '#' + next_slide);
    };

    var timer_widget_setup = function () {
        var timer_path = document.getElementById(TIMER_PATH_ID);
        timer_path.style.fill = '#dddddd';
        timer_path.onmouseover = function () {
            timer_path.style.fill = '#818181';
        };

        timer_path.onmouseout = function () {
            timer_path.style.fill = '#dddddd';
        };

        // put timer in a link
        var link_tag = reparent_node(timer_path, safe_common.SVG_NS_URI, "a");
        link_tag.setAttribute("id", TIMER_LINK_ID);
    };

    var get_slide_from_hash = function () {
        var to_ret = window.location.hash.length ? parseInt(window.location.hash.substr(1)) : 0;
        if (isNaN(to_ret)) {
            to_ret = 0;
        }
        return to_ret;
    };

    var set_hash_from_slide = function (slide_num) {
        window.location.hash = "#" + slide_num;
    };

    var onload = function () {
        safe_common.svg_instantiate_all_uses();

        // set up main content
        var _ret = safe_common.inkscape_move_main_content();
        var content_root = _ret[0];
        content_root.setAttribute("width", '100%');
        content_root.setAttribute("height", '100%');
        content_root.setAttribute("preserveAspectRatio", "xMidYMid");

        // maximize svg in window
        document.rootElement.setAttribute("width", "100%");
        document.rootElement.setAttribute("height", "100%");

        // set up download button logic
        var orig_button_group = safe_common.set_up_button("");

        // put button in another group element for transforming
        var button_group = reparent_node(orig_button_group, safe_common.SVG_NS_URI, "g");
        button_group.setAttribute("id", TOUR_BUTTON_ID);

        // set up timer
        timer_widget_setup();

        // set up arrow
        var arrow_parent = reparent_node(document.getElementById(ARROW_ID), safe_common.SVG_NS_URI, "g");
        arrow_parent.setAttribute("id", ARROW_GROUP_ID);
        var arrow_state = true;
        var arrow_blink_fn = function () {
            arrow_state = !arrow_state;
            safe_common.set_opacity(document.getElementById(ARROW_ID), arrow_state ? 1.0 : 0);
        };
        setInterval(arrow_blink_fn, 1000 / ARROW_BLINK_FREQUENCY);

        // move to the correct slide
        cur_slide = get_slide_from_hash();
        move_to_slide(cur_slide);

        // handle history manipulation
        if (window.onpopstate !== undefined) {
            window.addEventListener('popstate', function(event) {
                cur_slide = get_slide_from_hash();
                move_to_slide(cur_slide);
            });
        }
        else {
            // TODO: handle older browsers
            safe_common.assert(false, "implement this");
        }

        // size everything
        onresize();

        window.onresize = onresize;
    };

    var position_download_button = function () {
        var button_group = document.getElementById(TOUR_BUTTON_ID);
        var orig_button_bbox = button_group.getBBox();

        // move and scale download button
        var dpi = safe_common.get_dpi();
        var button_aspect_ratio = orig_button_bbox.height / orig_button_bbox.width;

        var new_button_width_px = dpi * BUTTON_WIDTH_INCHES;
        var new_button_height_px = new_button_width_px * button_aspect_ratio;

        var new_button_x = window.innerWidth - new_button_width_px - BUTTON_MARGIN_PX;
        var new_button_y = window.innerHeight - new_button_height_px - BUTTON_MARGIN_PX;

        button_group.setAttribute("transform",
                                  "translate(" +
                                  new_button_x + "," +
                                  new_button_y + ") "  +
                                  "scale(" +
                                  (new_button_width_px / orig_button_bbox.width) + "," +
                                  (new_button_height_px / orig_button_bbox.height) + ") " +
                                  "translate(" +
                                  (-orig_button_bbox.x) + "," +
                                  (-orig_button_bbox.y) + ") "
                                  );

        return {
            width: new_button_width_px,
            height: new_button_height_px,
            x: new_button_x,
            y: new_button_y,
        };
    };

    var timer_widget_set_percentage = function (percentage) {
        var timer_path = document.getElementById(TIMER_PATH_ID);

        var TIMER_SIZE_PX = TIMER_SIZE_INCHES * safe_common.get_dpi();

        var timer_center_x_px = TIMER_SIZE_PX / 2;
        var timer_center_y_px = TIMER_SIZE_PX / 2;

        var arc_pos_x = timer_center_x_px + (TIMER_SIZE_PX / 2) * Math.cos(Math.PI / 2 - percentage * 2 * Math.PI);
        var arc_pos_y = timer_center_y_px - (TIMER_SIZE_PX / 2) * Math.sin(Math.PI / 2 - percentage * 2 * Math.PI);

        var rotation = 0;
        var large_arc = (percentage % 1.0) >= 0.5 ? 0 : 1;
        var sweep = 0;
        var commands = ["M", TIMER_SIZE_PX / 2, TIMER_SIZE_PX / 2,
                        "v", -TIMER_SIZE_PX / 2,
                        "A", TIMER_SIZE_PX / 2 , TIMER_SIZE_PX / 2 , rotation,
                        large_arc, sweep, arc_pos_x, arc_pos_y,
                        ];
        timer_path.setAttribute("d", commands.join(" "));
    };

    var position_timer = function (download_button_rect) {
        var window_width_px = window.innerWidth;
        var window_height_px = window.innerHeight;

        var timer_path = document.getElementById(TIMER_PATH_ID);

        var TIMER_SIZE_PX = TIMER_SIZE_INCHES * safe_common.get_dpi();

        timer_path.setAttribute("transform",
                                "translate(" +
                                (window_width_px - BUTTON_MARGIN_PX - TIMER_SIZE_PX) + "," +
                                (download_button_rect.y - BUTTON_MARGIN_PX - TIMER_SIZE_PX) +
                                ")");
    };

    var position_arrow = function (download_button_rect) {
        var arrow_group = document.getElementById(ARROW_GROUP_ID);
        var bbox = arrow_group.getBBox();

        var arrow_width_px = ARROW_WIDTH_INCHES * safe_common.get_dpi();
        var arrow_height_px = bbox.height * arrow_width_px / bbox.width;

        arrow_group.setAttribute("transform",
                                 "translate(" +
                                 ((download_button_rect.width - arrow_width_px) / 2 + download_button_rect.x) + "," +
                                 (download_button_rect.y - BUTTON_MARGIN_PX - arrow_height_px) + ")" +
                                 "scale(" +
                                 (arrow_width_px / bbox.width) + "," +
                                 (arrow_height_px / bbox.height) + ") " +
                                 "translate(" + (-bbox.x) + "," + (-bbox.y) + ")");
    };

    var arrow_widget_set_visible = function (show) {
        safe_common.set_opacity(document.getElementById(ARROW_GROUP_ID), show ? 1.0 : 0);
    };

    var timer_widget_set_visible = function (show) {
        safe_common.set_opacity(document.getElementById(TIMER_PATH_ID), show ? 1.0 : 0);
    };

    var onresize = function () {
        // layout all content
        var download_button_rect = position_download_button();
        position_timer(download_button_rect);
        position_arrow(download_button_rect);
    };

    var show_right_slide = function (slide_num) {
        for (var i = 0; i < SLIDE_LAYER_IDS.length; ++i) {
            safe_common.inkscape_show_layer(document.getElementById(SLIDE_LAYER_IDS[i]),
                                            i == slide_num);
        }
    };

    var move_to_slide = function (slide_num) {
        if (slide_num >= SLIDE_LAYER_IDS.length) {
            slide_num = 0;
        }
        set_hash_from_slide(slide_num);
        if (slide_num == SLIDE_LAYER_IDS.length - 1) {
            stop_timer();
            arrow_widget_set_visible(true);
            timer_widget_set_visible(false);
        }
        else {
            arrow_widget_set_visible(false);
            timer_widget_set_visible(true);
            timer_widget_set_link(slide_num + 1);
            reset_timer();
        }
        show_right_slide(slide_num);
    };

    window.onload = onload;
})(safe_tour);
