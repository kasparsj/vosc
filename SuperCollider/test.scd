~visuals.sendMsg('/sounds', 1);
~visuals.sendMsg('/sound/stream', 0, "Microphone");

~visuals.sendMsg('/layers', 25, "grid");

~visuals.sendMsg('/tex/choose', "*", "shader");
~visuals.sendMsg('/tex/speed', "*", 0.1);

~visuals.sendMsg('/geom/choose', "*");

~visuals.sendMsg('/tex/color', "*", 1.0, 0.0, 0.0);
~visuals.sendMsg('/layer/alpha', "*", "tidal0gain");
~visuals.sendMsg('/tex/color', "*", "mfcc");
~visuals.sendMsg('/tex/reload', "*");
~visuals.sendMsg('/tex/unload', "*");
~visuals.sendMsg('/tex/noclear', "*", true);
~visuals.sendMsg('/tex', 0, "color.frag");
~visuals.sendMsg('/tex', 0, "shader:100fragments/OscCircle");
~visuals.sendMsg('/tex', 0, "NoisySpirals");
~visuals.sendMsg('/tex', 0, "black_hole.jpg");
//dont:~visuals.sendMsg('/tex', 0, "/Users/kasparsj/Desktop/Screenshot\ 2022-11-06\ at\ 02.57.56.png");
//dont:~visuals.sendMsg('/tex', 0, "http://1.bp.blogspot.com/-8PQ_NKjf5MI/UK_iuF92QUI/AAAAAAAAAYI/GV0NCn74Ly0/s1600/oh-happy-day.jpg");
~visuals.sendMsg('/layer/pos', "*", 0, 0);
~visuals.sendMsg('/layer/size', "*", 100, 100);
~visuals.sendMsg('/layer/scale', 0, 3);
~visuals.sendMsg('/tex/tint', "*", 256.rand, 256.rand, 256.rand);
~visuals.sendMsg('/tex', "*", "");
~visuals.sendMsg('/tex/unload', "*");
~visuals.sendMsg('/tex/choose', 0, "shader");
~visuals.sendMsg('/seek', "*", "rand");
~visuals.sendMsg('/reset', "*");

~visuals.sendMsg('/tint', 0, 0.15, 0.75, 1.0); // blue
~visuals.sendMsg('/tint', 0, 1.0, 0.45, 0); // orange
~visuals.sendMsg('/tint', 0, 0.4615, 0.615, 1.0); // blue2
~visuals.sendMsg('/tint', 0, 1.0, 1.0, 0.25); // yellow
~visuals.sendMsg('/tint', 0, 0, 0, 0); // black
~visuals.sendMsg('/tint', 0, 1.0, 1.0, 1.0); // white
~visuals.sendMsg('/tint', 1, 0, 0, 0); // white
~visuals.sendMsg('/tint', 0, 1.0, 0, 0); // red
~visuals.sendMsg('/tint', 0, 256.rand, 256.rand, 256.rand); // random
~visuals.sendMsg('/tint', 0, "rand"); // random

~visuals.sendMsg('/cam', true);
~visuals.sendMsg('/cam/pos', 0, 0, -100);

//~visuals.sendMsg('/onset/force'); // force onset
//~visuals.sendMsg('/onset', false); // toggle onset
~visuals.sendMsg('/data', 0, "const");
~visuals.sendMsg('/data', 0, "rand");
~visuals.sendMsg('/data', 0, "noise");
~visuals.sendMsg('/data', 1, "noise");
~visuals.sendMsg('/data', "*", "loud:0");
~visuals.sendMsg('/tex', 0, "shadertoy/AtmoScattering.frag");
~visuals.sendMsg('/tex', 0, "WaveClock");
~visuals.sendMsg('/tex', 1, "NoisySpirals");
~visuals.sendMsg('/reset', 0);

~visuals.sendMsg('/tex', 0, "webcam:0");
~visuals.sendMsg('/tex/loop', 0, 2, 60, 2.0);
~visuals.sendMsg('/size', 0, 100, 150);
~visuals.sendMsg('/size', 0, -100, -150);
~visuals.sendMsg('/scale', 0, -1);
~visuals.sendMsg('/align', 0, "center");
~visuals.sendMsg('/align', 0, "right", "bottom");
~visuals.sendMsg('/rot', 0, 0, 0, 90);
~visuals.sendMsg('/rot/point', 0, 0.5, 0.5);

~visuals.sendMsg('/cam', 1);
~visuals.sendMsg('/cam/pos', 0, 0, -3000, 10);
~visuals.sendMsg('/tex', 0, "0xff0000"); // load red color into first layer