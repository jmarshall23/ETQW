// Copyright (C) 2007 Id Software, Inc.
//
// ETQW GUI command model reconstructed from the Microsoft PDB layout and
// quakewars-hexrays/renderer/GuiModel.cpp.

#ifndef __GUI_MODEL_H__
#define __GUI_MODEL_H__

class idMaterial;
class idSoundEmitter;

class sdGuiModel {
public:
	sdGuiModel();

	void			BeginFrame();
	void			BeginRender();
	int				GetWritePos() const;

	void			BeginEmitToCurrentView( const float* modelMatrix, int allowInViewID, bool weaponDepthHack );
	void			BeginEmitFullScreen();
	void			End();
	void			RenderScene();
	void			EmitFullScreen( int end = -1 );

	void			SetColor( unsigned int color );
	void			SetRegister( int index, float value );
	void			SetRegisters( const float* values );
	void			EnableClipping( bool enable );
	void			PushClipRect( const sdBounds2D& rect );
	void			PopClipRect();

	void			DrawRect( float x, float y, float w, float h,
						float s1, float t1, float s2, float t2,
						const idMaterial* material, float angle );
	void			DrawClippedRect( float x, float y, float w, float h,
						float s1, float t1, float s2, float t2,
						const idMaterial* material, float angle );
	void			DrawMaskedClippedRect( float x, float y, float w, float h,
						float s01, float t01, float s02, float t02,
						float s11, float t11, float s12, float t12,
						const idMaterial* material, float angle );
	void			DrawCinematic( float x, float y, float w, float h,
						float s1, float t1, float s2, float t2,
						const idMaterial* material, idSoundEmitter* referenceSound, float angle );
	void			DrawClippedWinding( const idWinding2D& winding, const idMaterial* material );
	void			DrawClippedWindingMasked( const idWinding2D& winding, const idMaterial* material,
						float minx, float miny, float width, float height );

	void			SetFont( int font );
	void			SetFontSize( int pointSize );
	void			DrawTextA( const wchar_t* text, const sdBounds2D& rect, unsigned int flags );

	void			SubmitFrame( int windowWidth, int windowHeight );
	void			FlushFrame( int windowWidth, int windowHeight );

private:
	struct guiVertex_t {
		idVec2			xy;
		idVec2			st;
		idVec2			maskST;
	};

	struct guiPrimitive_t {
		const idMaterial*	material;
		idSoundEmitter*		referenceSound;
		unsigned int		color;
		float				materialParms[ MAX_ENTITY_SHADER_PARMS ];
		int					numVerts;
		guiVertex_t			verts[ idWinding2D::MAX_POINTS ];
		bool				masked;
	};

	struct guiText_t {
		idWStr				text;
		sdBounds2D			rect;
		unsigned int		flags;
		unsigned int		color;
		int					font;
		int					pointSize;
	};

	void			QueueRect( float x, float y, float w, float h,
						float s1, float t1, float s2, float t2,
						float ms1, float mt1, float ms2, float mt2,
						const idMaterial* material, idSoundEmitter* referenceSound,
						float angle, bool clipped, bool masked );
	void			QueueWinding( const idWinding2D& winding, const idMaterial* material,
						idSoundEmitter* referenceSound, bool masked,
						float minx, float miny, float width, float height );
	bool			ClipWinding( idWinding2D& winding ) const;

	idList< guiPrimitive_t >	primitives;
	idList< guiText_t >		texts;
	idList< sdBounds2D >		clipRects;
	unsigned int				currentColor;
	float						materialParms[ MAX_ENTITY_SHADER_PARMS ];
	bool						clippingEnabled;
	bool						fullScreen;
	int							currentFont;
	int							currentFontSize;
	int							writePosition;
};

extern sdGuiModel guiModel;

#endif /* !__GUI_MODEL_H__ */
