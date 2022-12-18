#include "Geom.h"
#include "Layer.h"

vector<string> Geom::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool Geom::isPrimitive(const string& path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string Geom::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

void Geom::load(const string& newPath, const vector<float>& args) {
    string newPrevPath = path;
    path = newPath;
    if (isPrimitive(newPath)) {
        if (loadPrimitive(args)) {
            usingModel = false;
            prevPath = newPrevPath;
            updateBoundingBox();
        }
        else {
            ofLogError() << ("could not load primitive: " + path);
            path = newPrevPath;
        }
    }
    else if (newPath == "quad") {
        loadQuad(args);
        usingModel = false;
        prevPath = newPrevPath;
        updateBoundingBox();
    }
    else if (newPath == "grass") {
        if (loadGrass(args)) {
            usingModel = false;
            prevPath = newPrevPath;
            updateBoundingBox();
        }
        else {
            ofLogError() << ("could not load grass: " + path);
            path = newPrevPath;
        }
    }
    else {
        if (loadModel(args)) {
            prevPath = newPrevPath;
            updateBoundingBox();
        }
        else {
            ofLogError() << ("could not load model: " + path);
            path = newPrevPath;
        }
    }
}

void Geom::load(const ofxOscMessage &m) {
    string newPath = m.getArgAsString(1);
    vector<float> args;
    for (int i=2; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    load(newPath, args);
}

bool Geom::loadModel(const vector<float>& args) {
    vector<string> modelPaths;
    if (!ofFilePath::isAbsolute(path)) {
        modelPaths.insert(modelPaths.end(), {
            ofToDataPath(path),
            ofToDataPath("models/" + path),
        });
    }
    else {
        modelPaths.insert(modelPaths.end(), {
            path,
        });
    }
    string modelPath = "";
    for (int i=0; i<modelPaths.size(); i++) {
        if (ofFile(modelPaths[i]).exists()) {
            modelPath = modelPaths[i];
            break;
        }
    }
    if (modelPath.find(".ply") != string::npos) {
        usingModel = false;
        mesh = NULL;
        mesh = make_shared<ofVboMesh>();
        mesh->load(modelPath);
        return true;
    }
    else {
        usingModel = true;
#if OF_VERSION_MAJOR==0 && OF_VERSION_MINOR<=11
#if OF_VERSION_MINOR<11 || OF_VERSION_PATCH <= 1
        // <= 0.11.1
        return model.loadModel(modelPath);
#else
        // > 0.11.1
        return model.load(modelPath);
#endif
#else
        // > 0.11
        return model.load(modelPath);
#endif
    }
}

bool Geom::loadPrimitive(const vector<float>& args) {
    if (path == "plane") {
        primitive = make_shared<ofPlanePrimitive>(args.size() > 0 ? args[0] : 100,
                                                  args.size() > 1 ? args[1] : 100,
                                                  args.size() > 2 ? args[2] : 2,
                                                  args.size() > 3 ? args[3] : 2);
    }
    else if (path == "box") {
        primitive = make_shared<ofBoxPrimitive>(args.size() > 0 ? args[0] : 100,
                                                args.size() > 1 ? args[1] : 100,
                                                args.size() > 2 ? args[2] : 100);
    }
    else if (path == "sphere") {
        primitive = make_shared<ofSpherePrimitive>(50, 16);
    }
    else if (path == "icosphere") {
        primitive = make_shared<ofIcoSpherePrimitive>(50, 2);
    }
    else if (path == "cylinder") {
        primitive = make_shared<ofCylinderPrimitive>();
    }
    else if (path == "cone") {
        primitive = make_shared<ofConePrimitive>();
    }
    if (primitive != NULL) {
        primitive->mapTexCoords(1.f, 1.f, 0.f, 0.f);
        mesh = NULL;
        mesh = shared_ptr<ofMesh>(primitive->getMeshPtr());
        return true;
    }
    return false;
}

bool Geom::loadQuad(const vector<float>& args) {
    mesh = NULL;
    mesh = make_shared<ofVboMesh>();
    
    // -1.0 to +1.0 is the full viewport (screen) if you use these as vertices in your vertex shader
    // (without multiplying by model, view, and projection matrices)    
    glm::vec3 vertices[4] =
    {
        ofVec3f(  1.0f,  1.0f, 0.0f ),
        ofVec3f( -1.0f,  1.0f, 0.0f ),
        ofVec3f(  1.0f, -1.0f, 0.0f ),
        ofVec3f( -1.0f, -1.0f, 0.0f )
    };
    
    // the 6 indices representing the two triangles making up our quad
    ofIndexType indices[6] =
    {
        0, 1, 2,
        2, 1, 3
    };
    
    // Texture coordinates vary from 0.0 to 1.0 when they are in normalized format
    // ofDisableArbTex() was called earlier set that we're using normalized texture coordinates
    glm::vec2 texCoords[4] =
    {
        ofVec2f( 1.0f, 1.0f ),
        ofVec2f( 0.0f, 1.0f ),
        ofVec2f( 1.0f, 0.0f ),
        ofVec2f( 0.0f, 0.0f )
    };
    
    mesh->addVertices( vertices, 4 );
    mesh->addTexCoords( texCoords, 4 );
    mesh->addIndices( indices, 6 );
}

bool Geom::loadGrass(const vector<float>& args) {
    int resX = args[0];
    int resY = args[0];
    
    ofVec3f size( args[1], 1, args[1] );
    ofVec3f placeStart = size * -0.5f;
    ofVec3f placeEnd   = size *  0.5f;
    
    mesh = NULL;
    mesh = make_shared<ofVboMesh>();
    mesh->setMode( OF_PRIMITIVE_LINES );
    
    for( int y = 0; y < resY; y++ )
    {
        float fracY = ofMap( y,  0, resY,  0, 1);
        for( int x = 0; x < resX; x++ )
        {
            float fracX = ofMap( x,  0, resX,  0, 1);
            
            ofVec3f tmpPos(0,0,0);
            tmpPos.x = ofLerp( placeStart.x, placeEnd.x, fracX );
            tmpPos.z = ofLerp( placeStart.z, placeEnd.z, fracY );
        
            ofVec2f perlinPos = ofVec2f(tmpPos.x, tmpPos.z) * args[2]; //ofMap( mx, 0, 1,  0.01, 5);
            
            float perlinVal = ofNoise( perlinPos.x, perlinPos.y );
            
            perlinVal = ofMap( perlinVal, args[3], 1,    0, 1, true ); // Remap the number taking our bottom threshold into account
            
            if( perlinVal > 0 )
            {
                mesh->addVertex( tmpPos );
                mesh->addVertex( tmpPos + ofVec3f(0,perlinVal,0) );
            }
        }
    }
    return true;
}

void Geom::choose(const ofxOscMessage& m) {
    string newPath = random();
    vector<float> args;
    for (int i=1; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }
    load(newPath, args);
}

void Geom::update() {
}

void Geom::oscCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/geom") {
        load(m);
    }
    else if (command == "/geom/choose") {
        choose(m);
    }
    else if (command == "/geom/set") {
        set(m);
    }
}

void Geom::updateBoundingBox() {
    if (usingModel) {
        boundingBox.max = model.getSceneMax() * model.getNormalizedScale();
        boundingBox.min = model.getSceneMin() * model.getNormalizedScale();
    }
    else {
        const vector<glm::vec3>& vertices  = mesh->getVertices();
        for(auto v : vertices){
            if (v.x < boundingBox.min.x) boundingBox.min.x = v.x;
            if (v.y < boundingBox.min.y) boundingBox.min.y = v.y;
            if (v.z < boundingBox.min.z) boundingBox.min.z = v.z;
            if (v.x > boundingBox.max.x) boundingBox.max.x = v.x;
            if (v.y > boundingBox.max.y) boundingBox.max.y = v.y;
            if (v.z > boundingBox.max.z) boundingBox.max.z = v.z;
        }
    }
}

void Geom::draw() const {
    if (usingModel) {
        model.drawFaces();
    }
    else {
        if (drawInstanced > 1) {
            if (drawWireframe) {
                dynamic_pointer_cast<ofVboMesh>(mesh)->drawInstanced(OF_MESH_WIREFRAME, drawInstanced);
            }
            else {
                dynamic_pointer_cast<ofVboMesh>(mesh)->drawInstanced(OF_MESH_FILL, drawInstanced);
            }
        }
        else {
            if (drawWireframe) {
                mesh->drawWireframe();
            }
            else {
                mesh->draw();
            }
        }
    }
}

void Geom::set(const ofxOscMessage& m) {
    string prop = m.getArgAsString(1);
    if (prop == "drawInstanced") {
        drawInstanced = m.getArgAsInt(2);
    }
    else if (prop == "meshMode") {
        getMesh().setMode(static_cast<ofPrimitiveMode>(m.getArgAsInt(2)));
    }
    else if (prop == "drawWireframe") {
        drawWireframe = m.getArgAsBool(2);
    }
}
