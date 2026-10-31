(function () {
  "use strict";

  var KEY_ALIASES = {
    LB: "lb",
    RB: "rb",
    A: "a",
    B: "b",
    X: "x",
    Y: "y"
  };

  var GLYPH_SIZES = {
    a: { w: 28, h: 28 },
    b: { w: 28, h: 28 },
    x: { w: 28, h: 28 },
    y: { w: 28, h: 28 },
    lb: { w: 34, h: 34 },
    rb: { w: 34, h: 34 }
  };

  var PLATFORMS = {
    Xbox: {
      base: "../shared/icons/Xbox/",
      files: {
        a: "T_X_A_White_Alt.png",
        b: "T_X_B_White_Alt.png",
        x: "T_X_X_White_Alt.png",
        y: "T_X_Y_White_Alt.png",
        lb: "T_X_LB_Alt.png",
        rb: "T_X_RB_Alt.png"
      }
    },
    Playstation: {
      base: "../shared/icons/Playstation/",
      files: {
        a: "T_P5_Cross_Alt.png",
        b: "T_P5_Circle_Alt.png",
        x: "T_P5_Square_Alt.png",
        y: "T_P5_Triangle_Alt.png",
        lb: "T_P5_L1_Alt.png",
        rb: "T_P5_R1_Alt.png"
      }
    },
    Switch: {
      base: "../shared/icons/Switch/",
      files: {
        a: "T_S_A_Alt.png",
        b: "T_S_B_Alt.png",
        x: "T_S_X_Alt.png",
        y: "T_S_Y_Alt.png",
        lb: "T_S_LB_Alt.png",
        rb: "T_S_RB_Alt.png"
      }
    }
  };

  var activePlatform = "Xbox";
  var tintCache = {};
  var WHITE_THRESHOLD = 200;
  var BLACK_THRESHOLD = 40;

  function glyphUrl(glyphId) {
    var platform = PLATFORMS[activePlatform] || PLATFORMS.Xbox;
    var file = platform.files[glyphId];
    if (!file) {
      return null;
    }
    return platform.base + file;
  }

  function getTintColor() {
    var style = getComputedStyle(document.documentElement);
    var amber = style.getPropertyValue("--co-amber").trim() || "#c9a84a";
    var hex = amber.replace("#", "");
    if (hex.length === 3) {
      hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
    }
    return {
      r: parseInt(hex.slice(0, 2), 16) || 201,
      g: parseInt(hex.slice(2, 4), 16) || 168,
      b: parseInt(hex.slice(4, 6), 16) || 74
    };
  }

  function getUiScale() {
    var style = getComputedStyle(document.documentElement);
    var scale = parseFloat(style.getPropertyValue("--co-ui-scale"));
    return isFinite(scale) && scale > 0 ? scale : 1;
  }

  function tintCacheKey(url, color) {
    return url + "|" + color.r + "," + color.g + "," + color.b;
  }

  function classifyPixel(r, g, b) {
    if (r >= WHITE_THRESHOLD && g >= WHITE_THRESHOLD && b >= WHITE_THRESHOLD) {
      return "white";
    }
    if (r <= BLACK_THRESHOLD && g <= BLACK_THRESHOLD && b <= BLACK_THRESHOLD) {
      return "black";
    }
    return "edge";
  }

  function tintImageData(imageData, color) {
    var data = imageData.data;
    var tr = color.r;
    var tg = color.g;
    var tb = color.b;

    for (var i = 0; i < data.length; i += 4) {
      var alpha = data[i + 3];
      if (alpha < 4) {
        data[i + 3] = 0;
        continue;
      }

      var r = data[i];
      var g = data[i + 1];
      var b = data[i + 2];
      var kind = classifyPixel(r, g, b);

      if (kind === "white") {
        data[i] = tr;
        data[i + 1] = tg;
        data[i + 2] = tb;
      } else if (kind === "black") {
        data[i] = 0;
        data[i + 1] = 0;
        data[i + 2] = 0;
      } else {
        var lum = (r + g + b) / (3 * 255);
        data[i] = Math.round(lum * tr);
        data[i + 1] = Math.round(lum * tg);
        data[i + 2] = Math.round(lum * tb);
      }
    }

    return imageData;
  }

  function tintImage(url, color, done) {
    var key = tintCacheKey(url, color);
    if (tintCache[key]) {
      done(tintCache[key]);
      return;
    }

    var img = new Image();
    img.onload = function () {
      var canvas = document.createElement("canvas");
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      var ctx = canvas.getContext("2d");
      if (!ctx) {
        done(null);
        return;
      }

      ctx.drawImage(img, 0, 0);
      var imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
      ctx.putImageData(tintImageData(imageData, color), 0, 0);
      var dataUrl = canvas.toDataURL("image/png");
      tintCache[key] = dataUrl;
      done(dataUrl);
    };
    img.onerror = function () {
      done(null);
    };
    img.src = url;
  }

  function hydrateGlyphs(root) {
    var scope = root || document;
    var color = getTintColor();
    var imgs = scope.querySelectorAll(".co-glyph-img[data-glyph-src]");

    for (var i = 0; i < imgs.length; i++) {
      (function (img) {
        var src = img.getAttribute("data-glyph-src");
        if (!src) {
          return;
        }
        tintImage(src, color, function (dataUrl) {
          if (dataUrl) {
            img.src = dataUrl;
          }
        });
      })(imgs[i]);
    }
  }

  window.CO_ControllerGlyphs = {
    setPlatform: function (platformName) {
      if (PLATFORMS[platformName]) {
        activePlatform = platformName;
        tintCache = {};
      }
    },

    getPlatform: function () {
      return activePlatform;
    },

    hydrate: hydrateGlyphs,

    onThemeApplied: function () {
      tintCache = {};
      hydrateGlyphs(document);
    },

    render: function (key) {
      var glyphId = KEY_ALIASES[String(key || "").toUpperCase()];
      if (!glyphId) {
        return '<span class="co-prompt-key co-prompt-key--fallback">' + key + "</span>";
      }

      var size = GLYPH_SIZES[glyphId] || GLYPH_SIZES.a;
      var scale = getUiScale();
      var url = glyphUrl(glyphId);
      if (!url) {
        return '<span class="co-prompt-key co-prompt-key--fallback">' + key + "</span>";
      }

      return (
        '<span class="co-prompt-key co-prompt-key--glyph co-prompt-key--' + glyphId + '"' +
        ' style="width:' + (size.w * scale) + "px;height:" + (size.h * scale) + 'px"' +
        ' aria-hidden="true">' +
        '<img class="co-glyph-img" data-glyph-src="' + url + '" src="' + url + '" alt="" draggable="false" />' +
        "</span>"
      );
    }
  };
})();
