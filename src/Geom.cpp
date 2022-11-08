#include "Geom.h"
#include "Layer.h"

vector<string> Geom::primitives = {"box", "sphere", "icosphere", "cylinder", "plane", "cone"};

bool Geom::isPrimitive(string path) {
    return find(primitives.begin(), primitives.end(), path) != primitives.end();
}

string Geom::random() {
    return primitives[int(ofRandom(primitives.size()))];
}

void Geom::load(string newPath, const vector<float>& args) {
    if (isPrimitive(newPath)) {
        if (primitive != NULL) {
            delete primitive;
            primitive = NULL;
        }
        prevPath = path;
        path = newPath;
        if (!loadPrimitive(args)) {
            ofLog() << "could not load primitive: " << path;
            path = prevPath;
            return;
        }
    }
    else if (newPath == "grass") {
        prevPath = path;
        path = newPath;
        if (!loadGrass(args)) {
            ofLog() << "could not load grass: " << path;
            path = prevPath;
            return;
        }
    }
    else {
        ofLog() << "invalid geom " << newPath;
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

bool Geom::loadPrimitive(const vector<float>& args) {
    if (path == "plane") {
        primitive = new ofPlanePrimitive(args.size() > 0 ? args[0] : 100,
                                         args.size() > 1 ? args[1] : 100,
                                         args.size() > 2 ? args[2] : 2,
                                         args.size() > 3 ? args[3] : 2);
    }
    else if (path == "box") {
        primitive = new ofBoxPrimitive(args.size() > 0 ? args[0] : 100,
                                       args.size() > 1 ? args[1] : 100,
                                       args.size() > 2 ? args[2] : 100);
    }
    else if (path == "sphere") {
        primitive = new ofSpherePrimitive(50, 16);
    }
    else if (path == "icosphere") {
        primitive = new ofIcoSpherePrimitive(50, 2);
    }
    else if (path == "cylinder") {
        primitive = new ofCylinderPrimitive();
    }
    else if (path == "cone") {
        primitive = new ofConePrimitive();
    }
    if (primitive != NULL) {
        //primitive->setPosition(pos);
        //primitive->setScale(size);
        //primitive->draw();
        mesh = primitive->getMesh();
        return true;
    }
    return false;
}

bool Geom::loadGrass(const vector<float>& args) {
    int resX = args[0];
    int resY = args[0];
    
    ofVec3f size( args[1], 1, args[1] );
    ofVec3f placeStart = size * -0.5f;
    ofVec3f placeEnd   = size *  0.5f;
    
    mesh.clear();
    mesh.setMode( OF_PRIMITIVE_LINES );
    
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
                mesh.addVertex( tmpPos );
                mesh.addVertex( tmpPos + ofVec3f(0,perlinVal,0) );
            }
        }
    }
    return true;
}

void Geom::choose() {
    path = random();
}

void Geom::update() {
}

void Geom::draw() {
    if (drawInstanced > 1) {
        mesh.drawInstanced(OF_MESH_FILL, drawInstanced);
    }
    else {
        mesh.draw();
    }
    if (drawWireframe) {
        mesh.drawWireframe();
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
