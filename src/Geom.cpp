#include "Geom.h"
#include "Layer.h"

vector<string> Geom::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool Geom::isPrimitive(const string& path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string Geom::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

void Geom::load(const string& newPath, const ofxOscMessage& m, int idx) {
    string newPrevPath = path;
    path = newPath;
    if (isPrimitive(newPath)) {
        if (loadPrimitive(m, idx)) {
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
        loadQuad(m, idx);
        usingModel = false;
        prevPath = newPrevPath;
        updateBoundingBox();
    }
    else if (newPath == "grass") {
        if (loadGrass(m, idx)) {
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
        if (loadModel(m, idx)) {
            prevPath = newPrevPath;
            updateBoundingBox();
        }
        else {
            ofLogError() << ("could not load model: " + path);
            path = newPrevPath;
        }
    }
}

void Geom::load(const ofxOscMessage &m, int idx) {
    string newPath = m.getArgAsString(idx);
    load(newPath, m, idx+1);
}

bool Geom::loadModel(const ofxOscMessage& m, int idx) {
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

void Geom::appendMesh(ofMesh mesh_, const glm::mat4 mat) {
    glm::mat4 normMat = glm::inverse(glm::transpose(mat));
    for (int i = 0; i < mesh_.getNumVertices(); i++) {
        mesh_.setVertex(i, mat * glm::vec4(mesh_.getVertex(i), 1.f));
        glm::vec3 n = normMat * glm::vec4(mesh_.getNormal(i), 0.f);
        mesh_.setNormal(i, glm::normalize(n));
    }
    mesh->append(mesh_);
}

bool Geom::loadPrimitive(const ofxOscMessage& m, int idx) {
    int num = 1;
    if (m.getArgType(idx) == OFXOSC_TYPE_STRING) {
        shared_ptr<BaseVar> var = BaseVar::createVar2(m, idx);
        var->update();
        num = var->size();
        mesh = NULL;
        bool retVal = true;
        for (int i=0; i<num; i++) {
            retVal &= appendPrimitive(var, i);
        }
        var = NULL;
        return retVal;
    }
    else {
        vector<float> args;
        for (int i=idx; i<m.getNumArgs(); i++) {
            args.push_back(m.getArgAsFloat(i));
        }
        mesh = NULL;
        bool retVal = true;
        for (int i=0; i<num; i++) {
            retVal &= appendPrimitive(createPrimitive(args));
        }
        return retVal;
    }
}

bool Geom::appendPrimitive(const shared_ptr<BaseVar>& var, int i) {
    shared_ptr<Variable<glm::vec3>> vec3 = dynamic_pointer_cast<Variable<glm::vec3>>(var);
    if (vec3 != NULL) {
        const shared_ptr<of3dPrimitive> primitive = createPrimitive();
        primitive->setScale(vec3->get(i));
        return appendPrimitive(primitive);
    }
    else {
        shared_ptr<Variable<ofxExprNode>> node = dynamic_pointer_cast<Variable<ofxExprNode>>(var);
        if (node != NULL) {
            const shared_ptr<of3dPrimitive> primitive = createPrimitive();
            primitive->setPosition(node->get(i).getPosition());
            primitive->setOrientation(node->get(i).getOrientation());
            primitive->setScale(node->get(i).getScale());
            return appendPrimitive(primitive);
        }
        else {
            shared_ptr<Variable<glm::mat4>> mat4 = dynamic_pointer_cast<Variable<glm::mat4>>(var);
            if (mat4 != NULL) {
                const shared_ptr<of3dPrimitive> primitive = createPrimitive();
                // todo: how can I update matrix directly?
                return appendPrimitive(primitive);
            }
        }
    }
    return false;
}

bool Geom::appendPrimitive(const shared_ptr<of3dPrimitive>& primitive) {
    if (primitive != NULL) {
        primitive->mapTexCoords(1.f, 1.f, 0.f, 0.f);
        if (mesh == NULL) {
            mesh = make_shared<ofVboMesh>();
        }
        appendMesh(primitive->getMesh(), primitive->getGlobalTransformMatrix());
        return true;
    }
    return false;
}

shared_ptr<of3dPrimitive> Geom::createPrimitive() {
    vector<float> args;
    return createPrimitive(args);
}

shared_ptr<of3dPrimitive> Geom::createPrimitive(const vector<float>& args) {
    if (path == "plane") {
        return make_shared<ofPlanePrimitive>(args.size() > 0 ? args[0] : 100,
                                                  args.size() > 1 ? args[1] : 100,
                                                  args.size() > 2 ? args[2] : 2,
                                                  args.size() > 3 ? args[3] : 2);
    }
    else if (path == "box") {
        return make_shared<ofBoxPrimitive>(args.size() > 0 ? args[0] : 100,
                                                args.size() > 1 ? args[1] : 100,
                                                args.size() > 2 ? args[2] : 100);
    }
    else if (path == "sphere") {
        return make_shared<ofSpherePrimitive>(50, 16);
    }
    else if (path == "icosphere") {
        return make_shared<ofIcoSpherePrimitive>(50, 2);
    }
    else if (path == "cylinder") {
        return make_shared<ofCylinderPrimitive>();
    }
    else if (path == "cone") {
        return make_shared<ofConePrimitive>();
    }
    return NULL;
}

bool Geom::loadQuad(const ofxOscMessage& m, int idx) {
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

bool Geom::loadGrass(const ofxOscMessage& m, int idx) {
    vector<float> args;
    for (int i=idx; i<m.getNumArgs(); i++) {
        args.push_back(m.getArgAsFloat(i));
    }

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
    load(newPath, m, 0);
}

void Geom::update() {
    // todo: check if matrix is time dependant
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
    else if (command == "/geom/pos") {
        // todo: translate mesh
    }
    else if (command == "/geom/rot") {
        // todo: rotate mesh
    }
    else if (command == "/geom/scale") {
        // todo: scale mesh
    }
    else if (command == "/geom/color") {
        // todo: set mesh colors
    }
    else if (command == "/geom/boxes") {
        // matrices mat4
        // colors ofFloatColor
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
