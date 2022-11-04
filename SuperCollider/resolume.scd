~visuals = NetAddr("127.0.0.1", 7000);
~visuals.sendMsg('/composition/selectedlayer/video/mixer/blendmode', 60.rand);
~visuals.sendMsg('/composition/selectedlayer/video/effects/transform/positionx', 1.0.rand);
~visuals.sendMsg('/composition/selectedlayer/video/effects/transform/positiony', 1.0.rand);