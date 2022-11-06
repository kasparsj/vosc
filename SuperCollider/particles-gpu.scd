(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/geom', 0, "box", 0.0015, 0.0015, 0.01);
~visuals.sendMsg('/geom/mesh/mode', 0, 0);
~visuals.sendMsg('/geom/instanced', 0, 128 * 128);
~visuals.sendMsg('/shader', 0, "particles/DrawInstancedGeometry");
//~visuals->setUniform2f("u_resolution", particleDataFbo.source()->getWidth(), particleDataFbo.source()->getHeight() );
~visuals.sendMsg('/shader/uniform', 0, "particleMaxAge", 10);
//~visuals->setUniform1i("numLights", 1 );
~visuals.sendMsg('/shader/uniform', 0, "particleStartColor", 255, 255, 255);
~visuals.sendMsg('/shader/uniform', 0, "particleEndColor", 0, 0, 0);
~visuals.sendMsg('/data/shader', 0, "particles/Update");
~visuals.sendMsg('/data/shader/uniform', 0, "timeStep", 1.0 / 60.0);
~visuals.sendMsg('/data/shader/uniform', 0, "particleMaxAge", 10);
~visuals.sendMsg('/data/shader/uniform', 0, "noisePositionScale", 1.5);
~visuals.sendMsg('/data/shader/uniform', 0, "noiseTimeScale", 1.0 / 4000.0);
~visuals.sendMsg('/data/shader/uniform', 0, "noisePersistence", 0.2);
~visuals.sendMsg('/data/shader/uniform', 0, "noiseMagnitude", 0.075);
~visuals.sendMsg('/data/shader/uniform', 0, "baseSpeed", 0.5, 0, 0);
~visuals.sendMsg('/data/fbo', 0, 128, 128, 2);
~visuals.sendMsg('/cam', 1);
)