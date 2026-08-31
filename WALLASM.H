void rectTransform(Fixed32 wx,Fixed32 wy,Fixed32 wz,
		   int light,int h,int w,
		   Fixed32 px,Fixed32 py,Fixed32 pz,
		   Fixed32 hx,Fixed32 hy,Fixed32 hz,
		   void *output,
		   unsigned short (*lightFunc)(char vLightIndex,
					       MthXyz *pos),
		   int lightStepExtra,   /* GCC14: extra bytes after each light byte */
		   int lightRowExtra);   /* GCC14: extra bytes after each vertex row */

void normTransform(sVertexType *firstVertex,MthMatrix *viewMatrix,
		   int nmVert,void *output,
		   unsigned short (*lightFunc)(char vLightIndex,
					       MthXyz *pos));
