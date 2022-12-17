//
//  FboPingPong.h
//  emptyExample
//
//  Created by Andreas Müller on 12/08/2013.
//
//

#pragma once

#include "ofMain.h"

class FboPingPong
{
	public:

        bool isAllocated() {
            return fbo1.isAllocated() && fbo2.isAllocated();
        }
		void allocate( int _w, int _h, int internalformat = GL_RGB, ofColor _clearColor = ofColor(255,255,255) );
		void allocate( ofFbo::Settings _settings, ofColor _clearColor = ofColor(255,255,255) );
	
		ofFbo* source() { return sourceBuffer;	}
        const ofFbo* source() const { return sourceBuffer;    }
		ofFbo* dest()	{ return destBuffer;	}
	
		void draw( glm::vec2 _pos, float _width, bool _drawBack = false );
	
		void clearBoth();
		void clearBoth( ofColor _clearColor );
	
		void clearSource();
		void clearDest();
	
		void clearSource( ofColor _clearColor );
		void clearDest( ofColor _clearColor );
	
		void setClearColor( ofColor _color );
	
		void swap();
		
	private:
	
		ofFbo* sourceBuffer;
		ofFbo* destBuffer;
	
		ofFbo fbo1;
		ofFbo fbo2;
		
		ofColor clearColor;
};
