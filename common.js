var safe_common = {};

(function (exports) {
    exports.RuntimeException = function (message) {
        this.message = message;
        this.name = "UserException";
    }

    exports.assert = function (bool, msg) {
        if (bool) return;
        alert(msg);
    };
})(safe_common);
