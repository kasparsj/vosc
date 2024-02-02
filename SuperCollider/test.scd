~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

~visuals.send('/input', 0, "Microphone");
~visuals.send('/tex', "webcam", "webcam:0");

~visuals.send('/layers', 9, "grid");
~visuals.send('/layers', 1, "stack");
~visuals.send('/layers', 25, "grid");

~visuals.send('/tex/choose', "*", "video");
~visuals.send('/tex', "*", "webcam");
~visuals.send('/tex/speed', "*", 0.1);
~visuals.send('/tex/set', "*", "blendMode", 5); // set blendMode to multiply

~visuals.send('/geom/choose', "*");

~visuals.send('/tex/color', "*", 1.0, 0.0, 0.0);
~visuals.send('/layer/alpha', "*", 0.5);
~visuals.send('/layer/alpha', "*", "mic");
//~visuals.send('/tex/color', "*", "mfcc");
~visuals.send('/tex/reload', "*");
~visuals.send('/tex/unload', "*");
~visuals.send('/tex/noclear', "*", true);
~visuals.send('/tex', 0, "color.frag");
~visuals.send('/tex', 0, "shader:100fragments/OscCircle");
~visuals.send('/tex', 0, "shader:shadertoy/Energetic.frag");
~visuals.send('/tex', 0, "NoisySpirals");
~visuals.send('/tex', 0, "black_hole.jpg");
//dont:~visuals.send('/tex', 0, "/Users/kasparsj/Desktop/Screenshot\ 2022-11-06\ at\ 02.57.56.png");
//dont:~visuals.send('/tex', 0, "http://1.bp.blogspot.com/-8PQ_NKjf5MI/UK_iuF92QUI/AAAAAAAAAYI/GV0NCn74Ly0/s1600/oh-happy-day.jpg");
~visuals.send('/layer/pos', "*", 0, 0);
~visuals.send('/layer/rot', "*", 0, 360.rand, 360.rand, 10);
~visuals.send('/cam', "easy");
~visuals.send('/layer/size', "*", 800, 800);
~visuals.send('/layer/scale', 0, 1.3);
~visuals.send('/layer/tint', "*", 256.rand, 256.rand, 256.rand);
~visuals.send('/layer/tint', "*", 1.0, 1.0, 1.0);
~visuals.send('/tex', "*", "");
~visuals.send('/tex/unload', "*");
~visuals.send('/tex/choose', 0, "shader");
~visuals.send('/seek', "*", "rand");
~visuals.send('/reset', "*");

~visuals.send('/tint', 0, 0.15, 0.75, 1.0); // blue
~visuals.send('/tint', 0, 1.0, 0.45, 0); // orange
~visuals.send('/tint', 0, 0.4615, 0.615, 1.0); // blue2
~visuals.send('/tint', 0, 1.0, 1.0, 0.25); // yellow
~visuals.send('/tint', 0, 0, 0, 0); // black
~visuals.send('/tint', 0, 1.0, 1.0, 1.0); // white
~visuals.send('/tint', 1, 0, 0, 0); // white
~visuals.send('/tint', 0, 1.0, 0, 0); // red
~visuals.send('/tint', 0, 256.rand, 256.rand, 256.rand); // random
~visuals.send('/tint', 0, "rand"); // random

~visuals.send('/cam', true);
~visuals.send('/cam/pos', 0, 0, -100);

//~visuals.send('/onset/force'); // force onset
//~visuals.send('/onset', false); // toggle onset
~visuals.send('/data', 0, "const");
~visuals.send('/data', 0, "rand");
~visuals.send('/data', 0, "noise");
~visuals.send('/data', 1, "noise");
~visuals.send('/data', "*", "loud:0");
~visuals.send('/tex', 0, "shadertoy/AtmoScattering.frag");
~visuals.send('/tex', 0, "WaveClock");
~visuals.send('/tex', 1, "NoisySpirals");
~visuals.send('/reset', 0);

~visuals.send('/tex', 0, "webcam:0");
~visuals.send('/tex/loop', 0, 2, 60, 2.0);
~visuals.send('/size', 0, 100, 150);
~visuals.send('/size', 0, -100, -150);
~visuals.send('/layer/scale', 0, -1);
~visuals.send('/align', 0, "center");
~visuals.send('/align', 0, "right", "bottom");
~visuals.send('/rot', 0, 0, 0, 90);
~visuals.send('/rot/point', 0, 0.5, 0.5);

~visuals.send('/cam', "easy");
~visuals.send('/cam/pos', 0, 0, 3000, 10);
~visuals.send('/tex', 0, "0xff0000"); // load red color into first layer
