

#include <spine/Animation.h>
#include <spine/Array.h>
#include <spine/AtlasAttachmentLoader.h>
#include <spine/SkeletonBinary.h>
#include <spine/extension.h>

#include <stdint.h>

_SP_ARRAY_DECLARE_TYPE(spTimelineArray, spTimeline*)
_SP_ARRAY_IMPLEMENT_TYPE(spTimelineArray, spTimeline*)

typedef struct {
	const unsigned char* cursor;
	const unsigned char* end;
} _dataInput;

typedef struct {
	spSkeletonBinary super;
	int ownsLoader;
} _spSkeletonBinary;

/*====================  Internal free functions ====================*/

static unsigned char readByte(_dataInput* input) {
	return *input->cursor++;
}

static signed char readSByte(_dataInput* input) {
	return (signed char)readByte(input);
}

static int readBoolean(_dataInput* input) {
	return readByte(input) != 0;
}

static int readInt(_dataInput* input) {
	uint32_t result = readByte(input);
	result <<= 8;
	result |= readByte(input);
	result <<= 8;
	result |= readByte(input);
	result <<= 8;
	result |= readByte(input);
	return (int)result;
}

static int readVarint(_dataInput* input, int /*bool*/ optimizePositive) {
	unsigned char b = readByte(input);
	int32_t value = b & 0x7F;
	if (b & 0x80) {
		b = readByte(input);
		value |= (b & 0x7F) << 7;
		if (b & 0x80) {
			b = readByte(input);
			value |= (b & 0x7F) << 14;
			if (b & 0x80) {
				b = readByte(input);
				value |= (b & 0x7F) << 21;
				if (b & 0x80) value |= (readByte(input) & 0x7F) << 28;
			}
		}
	}
	if (!optimizePositive) value = (((unsigned int)value >> 1) ^ -(value & 1));
	return value;
}

static float readFloat(_dataInput* input) {
	union {
		int intValue;
		float floatValue;
	} intToFloat;
	intToFloat.intValue = readInt(input);
	return intToFloat.floatValue;
}

static char* readString(_dataInput* input) {
	int length = readVarint(input, 1);
	char* string;
	if (length == 0) return NULL;
	string = MALLOC(char, length);
	memcpy(string, input->cursor, length - 1);
	input->cursor += length - 1;
	string[length - 1] = '\0';
	return string;
}

static void readColor(_dataInput* input, float* r, float* g, float* b, float* a) {
	*r = readByte(input) / 255.0f;
	*g = readByte(input) / 255.0f;
	*b = readByte(input) / 255.0f;
	*a = readByte(input) / 255.0f;
}

static void readFloatArray(_dataInput* input, float scale, float** data, int *size)
{
	*size = readVarint(input, 1);
	*data = MALLOC(float, *size);
	for (int i = 0; i < *size; ++i)
	{
		*(*data + i) = readFloat(input) * scale;
	}
}

static void readShortArray(_dataInput* input, int** data, int* size)
{
	*size = readVarint(input, 1);
	*data = MALLOC(int, *size);
	for (int i = 0; i < *size; ++i)
	{
		*(*data + i) = readByte(input) << 8;
		*(*data + i) |= readByte(input);
	}
}

static void readIntArray(_dataInput* input, int** data, int* size)
{
	*size = readVarint(input, 1);
	*data = MALLOC(int, *size);
	for (int i = 0; i < *size; ++i)
	{
		*(*data + i) = readVarint(input, 1);
	}
}

/* Should not use spTimelineType enumeration when reading timelines. */
typedef enum {
	SP_BINARY_TIMELINE_SCALE,
	SP_BINARY_TIMELINE_ROTATE,
	SP_BINARY_TIMELINE_TRANSLATE,
	SP_BINARY_TIMELINE_ATTACHMENT,
	SP_BINARY_TIMELINE_COLOR,
	SP_BINARY_TIMELINE_FLIPX,
	SP_BINARY_TIMELINE_FLIPY
}spTimeLineBinaryType;

typedef enum {
	SP_BINARY_CURVE_LINEAR,
	SP_BINARY_CURVE_STEPPED,
	SP_BINARY_CURVE_BEZIER
}spCurveBinaryType;

static void readCurve(_dataInput* input, spCurveTimeline* timeline, int frameIndex) {
	switch (readByte(input)) {
	case SP_BINARY_CURVE_STEPPED: {
		spCurveTimeline_setStepped(timeline, frameIndex);
		break;
	}
	case SP_BINARY_CURVE_BEZIER: {
		float cx1 = readFloat(input);
		float cy1 = readFloat(input);
		float cx2 = readFloat(input);
		float cy2 = readFloat(input);
		spCurveTimeline_setCurve(timeline, frameIndex, cx1, cy1, cx2, cy2);
		break;
	}
	}
}

/*====================  Internal class functions  ====================*/

static void spSkeletonBinary_setError_(spSkeletonBinary* self, const char* value1, const char* value2) {
	char message[256];
	int length;
	FREE(self->error);
	strcpy(message, value1);
	length = (int)strlen(value1);
	if (value2) strncat(message + length, value2, 255 - length);
	MALLOC_STR(self->error, message);
}

static spAttachment* spSkeletonBinary_readAttachment_(spSkeletonBinary* self, _dataInput* input,
	spSkin* skin, int slotIndex, const char* attachmentName,
	spSkeletonData* skeletonData, int /*bool*/ nonessential)
{
	const char* name = readString(input);
	int nameToBeFreed = name != NULL;
	if (!nameToBeFreed) {
		name = attachmentName;
	}

	spAttachmentType attachmentype = readByte(input);

	switch (attachmentype)
	{
	case SP_ATTACHMENT_REGION:
	{
		char* path = readString(input);
		if (path == NULL)MALLOC_STR(path, name);

		spAttachment* attachment = spAttachmentLoader_newAttachment(self->attachmentLoader, skin, attachmentype, name, path);
		if (nameToBeFreed)FREE(name);
		if (attachment == NULL) return NULL;
		spRegionAttachment* regionAttachment = SUB_CAST(spRegionAttachment, attachment);
		regionAttachment->path = path;
		regionAttachment->x = readFloat(input) * self->scale;
		regionAttachment->y = readFloat(input) * self->scale;
		regionAttachment->scaleX = readFloat(input);
		regionAttachment->scaleY = readFloat(input);
		regionAttachment->rotation = readFloat(input);
		regionAttachment->width = readFloat(input) * self->scale;
		regionAttachment->height = readFloat(input) * self->scale;
		readColor(input, &regionAttachment->r, &regionAttachment->g, &regionAttachment->b, &regionAttachment->a);

		spRegionAttachment_updateOffset(regionAttachment);

		return attachment;
	}
	case SP_ATTACHMENT_BOUNDING_BOX:
	{
		spAttachment* attachment = spAttachmentLoader_newAttachment(self->attachmentLoader, skin, attachmentype, name, NULL);
		if (nameToBeFreed)FREE(name);
		if (attachment == NULL)return NULL;
		spBoundingBoxAttachment* boxAttachment = SUB_CAST(spBoundingBoxAttachment, attachment);
		readFloatArray(input, self->scale, &boxAttachment->vertices, &boxAttachment->verticesCount);
		return attachment;
	}
	case SP_ATTACHMENT_MESH:
	{
		char* path = readString(input);
		if (path == NULL)MALLOC_STR(path, name);

		spAttachment* attachment = spAttachmentLoader_newAttachment(self->attachmentLoader, skin, attachmentype, name, path);
		if (nameToBeFreed)FREE(name);
		if (attachment == NULL) return NULL;

		spMeshAttachment* meshAttachment = SUB_CAST(spMeshAttachment, attachment);
		meshAttachment->path = path;

		int uvCount = 0;
		readFloatArray(input, self->scale, &meshAttachment->regionUVs, &uvCount);
		readShortArray(input, &meshAttachment->triangles, &meshAttachment->trianglesCount);
		readFloatArray(input, self->scale, &meshAttachment->vertices, &meshAttachment->verticesCount);
		readColor(input, &meshAttachment->r, &meshAttachment->g, &meshAttachment->b, &meshAttachment->a);
		meshAttachment->hullLength = readVarint(input, 1) * 2;

		if (nonessential) {
			readIntArray(input, &meshAttachment->edges, &meshAttachment->edgesCount);
			meshAttachment->width = readFloat(input) * self->scale;
			meshAttachment->height = readFloat(input) * self->scale;
		}

		spMeshAttachment_updateUVs(meshAttachment);

		return attachment;
	}
	case SP_ATTACHMENT_SKINNED_MESH:
	{
		char* path = readString(input);
		if (path == NULL)MALLOC_STR(path, name);

		spAttachment* attachment = spAttachmentLoader_newAttachment(self->attachmentLoader, skin, attachmentype, name, path);
		if (nameToBeFreed)FREE(name);
		if (attachment == NULL) return NULL;

		spSkinnedMeshAttachment* skinnedMeshAttachment = SUB_CAST(spSkinnedMeshAttachment, attachment);

		readFloatArray(input, self->scale, &skinnedMeshAttachment->regionUVs, &skinnedMeshAttachment->uvsCount);
		readShortArray(input, &skinnedMeshAttachment->triangles, &skinnedMeshAttachment->trianglesCount);

		int vertexCount = readVarint(input, 1);

		spIntArray* bones = spIntArray_create(skinnedMeshAttachment->uvsCount * 3);
		spFloatArray* weights = spFloatArray_create(skinnedMeshAttachment->uvsCount * 3 * 3);

		for (int i = 0; i < vertexCount; ++i) {
			int boneCount = (int)readFloat(input);
			spIntArray_add(bones, boneCount);
			for (int ii = i + boneCount * 4; i < ii; i += 4) {
				spIntArray_add(bones, (int)readFloat(input));
				spFloatArray_add(weights, readFloat(input) * self->scale);
				spFloatArray_add(weights, readFloat(input) * self->scale);
				spFloatArray_add(weights, readFloat(input));
			}
		}

		skinnedMeshAttachment->bonesCount = bones->size;
		skinnedMeshAttachment->bones = bones->items;
		skinnedMeshAttachment->weightsCount = weights->size;
		skinnedMeshAttachment->weights = weights->items;

		FREE(bones);
		FREE(weights);

		readColor(input, &skinnedMeshAttachment->r, &skinnedMeshAttachment->g, &skinnedMeshAttachment->b, &skinnedMeshAttachment->a);
		skinnedMeshAttachment->hullLength = readVarint(input, 1);

		if (nonessential) {
			readIntArray(input, &skinnedMeshAttachment->edges, &skinnedMeshAttachment->edgesCount);
			skinnedMeshAttachment->width = readFloat(input) * self->scale;
			skinnedMeshAttachment->height = readFloat(input) * self->scale;
		}

		spSkinnedMeshAttachment_updateUVs(skinnedMeshAttachment);

		return attachment;
	}
	} /*switch*/

	return NULL;
}

static spSkin* spSkeletonBinary_readSkin_(spSkeletonBinary* self, _dataInput* input, const char* skinName, spSkeletonData* skeletonData, int /*bool*/ nonessential)
{
	int slotCount = readVarint(input, 1);
	if (slotCount == 0) return NULL;

	spSkin* skin = spSkin_create(skinName);

	for (int i = 0; i < slotCount; ++i)
	{
		int slotIndex = readVarint(input, 1);
		for (int ii = 0, nn = readVarint(input, 1); ii < nn; ++ii) {
			const char* name = readString(input);
			spAttachment* attachment = spSkeletonBinary_readAttachment_(self, input, skin, slotIndex, name, skeletonData, nonessential);
			if (attachment) spSkin_addAttachment(skin, slotIndex, name, attachment);
			FREE(name);
		}
	}
	return skin;
}

static spAnimation* spSkeletonBinary_readAnimation_(spSkeletonBinary* self, const char* name,
	_dataInput* input, spSkeletonData* skeletonData)
{
	spTimelineArray *timelines = spTimelineArray_create(256);
	float duration = 0;

	/* Slot timelines. */
	for (int i = 0, n = readVarint(input, 1); i < n; ++i) {
		int slotIndex = readVarint(input, 1);
		for (int ii = 0, nn = readVarint(input, 1); ii < nn; ++ii) {
			int timelineType = readByte(input);
			int frameCount = readVarint(input, 1);
			switch (timelineType) {
			case SP_BINARY_TIMELINE_ATTACHMENT:
			{
				spAttachmentTimeline* timeline = spAttachmentTimeline_create(frameCount);
				timeline->slotIndex = slotIndex;
				for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
					float time = readFloat(input);
					const char* attachmentName = readString(input);
					spAttachmentTimeline_setFrame(timeline, frameIndex, time, attachmentName);
					FREE(attachmentName);
				}
				spTimelineArray_add(timelines, SUPER(timeline));
				duration = MAX(duration, timeline->frames[frameCount - 1]);
				break;
			}
			case SP_BINARY_TIMELINE_COLOR:
			{
				spColorTimeline* timeline = spColorTimeline_create(frameCount);
				timeline->slotIndex = slotIndex;
				for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
					float time = readFloat(input);
					float r, g, b, a;
					readColor(input, &r, &g, &b, &a);
					spColorTimeline_setFrame(timeline, frameIndex, time, r, g, b, a);
					if (frameIndex < frameCount - 1) readCurve(input, SUPER(timeline), frameIndex);
				}
				spTimelineArray_add(timelines, SUPER(SUPER(timeline)));
				duration = MAX(duration, timeline->frames[(frameCount * 5 - 5)]);
				break;
			}
			default :
				spTimelineArray_dispose(timelines);
				return NULL;
			} /*switch*/
		}
	}

	/* Bone timelines. */
	for (int i = 0, n = readVarint(input, 1); i < n; ++i) {
		int boneIndex = readVarint(input, 1);
		for (int ii = 0, nn = readVarint(input, 1); ii < nn; ++ii) {
			unsigned char timelineType = readByte(input);
			int frameCount = readVarint(input, 1);
			switch (timelineType) {
			case SP_BINARY_TIMELINE_ROTATE:
			{
				spRotateTimeline* timeline = spRotateTimeline_create(frameCount);
				timeline->boneIndex = boneIndex;
				for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
					float time = readFloat(input);
					float degrees = readFloat(input);
					spRotateTimeline_setFrame(timeline, frameIndex, time, degrees);
					if (frameIndex < frameCount - 1) readCurve(input, SUPER(timeline), frameIndex);
				}
				spTimelineArray_add(timelines, SUPER(SUPER(timeline)));
				duration = MAX(duration, timeline->frames[frameCount * 2 - 2]);
				break;
			}
			case SP_BINARY_TIMELINE_TRANSLATE:
			case SP_BINARY_TIMELINE_SCALE:
			{
				spTranslateTimeline* timeline = NULL;
				float timelineScale = 1;
				if (timelineType == SP_BINARY_TIMELINE_SCALE) {
					timeline = spScaleTimeline_create(frameCount);
				}
				else {
					timeline = spTranslateTimeline_create(frameCount);
					timelineScale = self->scale;
				}

				timeline->boneIndex = boneIndex;
				for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
					float time = readFloat(input);
					float x = readFloat(input) * timelineScale;
					float y = readFloat(input) * timelineScale;
					if (timelineType == SP_BINARY_TIMELINE_SCALE) {
						spScaleTimeline_setFrame(timeline, frameIndex, time, x, y);
					}
					else {
						spTranslateTimeline_setFrame(timeline, frameIndex, time, x, y);
					}
					if (frameIndex < frameCount - 1) readCurve(input, SUPER(timeline), frameIndex);
				}

				spTimelineArray_add(timelines, SUPER(SUPER(timeline)));
				duration = MAX(duration, timeline->frames[frameCount * 3 - 3]);
				break;
			}
			case SP_BINARY_TIMELINE_FLIPX:
			case SP_BINARY_TIMELINE_FLIPY:
			{
				spFlipTimeline* timeline = spFlipTimeline_create(frameCount, timelineType == SP_TIMELINE_FLIPX ? 1 : 0);
				timeline->boneIndex = boneIndex;
				for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
					float time = readFloat(input);
					int flip = readBoolean(input);
					spFlipTimeline_setFrame(timeline, frameIndex, time, flip);
				}
				spTimelineArray_add(timelines, SUPER(timeline));
				duration = MAX(duration, timeline->frames[frameCount * 2 - 2]);
				break;
			}
			default:
				spTimelineArray_dispose(timelines);
				return NULL;
			} /*switch*/
		}
	}

	/* IK constraint timelines. */
	for (int i = 0, n = readVarint(input, 1); i < n; ++i) {
		int index = readVarint(input, 1);
		int frameCount = readVarint(input, 1);
		spIkConstraintTimeline* timeline = spIkConstraintTimeline_create(frameCount);
		/*CS seems to check the existence of IkConstraintData in skeletonData, and set -1 if not found.*/
		timeline->ikConstraintIndex = index < skeletonData->ikConstraintsCount ? index : -1;
		for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			float time = readFloat(input);
			float mix = readFloat(input);
			char bendDirection = readSByte(input);
			spIkConstraintTimeline_setFrame(timeline, frameIndex, time, mix, bendDirection);
			if (frameIndex < frameCount - 1) readCurve(input, SUPER(timeline), frameIndex);
		}
		spTimelineArray_add(timelines, SUPER(SUPER(timeline)));
		duration = MAX(duration, timeline->frames[frameCount * 3 - 3]);
	}

	/* FFD timelines. */
	for (int i = 0, n = readVarint(input, 1); i < n; ++i) {
		spSkin* skin = skeletonData->skins[readVarint(input, 1)];
		for (int ii = 0, nn = readVarint(input, 1); ii < nn; ++ii) {
			int slotIndex = readVarint(input, 1);
			for (int iii = 0, nnn = readVarint(input, 1); iii < nnn; ++iii) {
				const char* attachmentName = readString(input);
				int frameCount = readVarint(input, 1);

				spAttachment* attachment = spSkin_getAttachment(skin, slotIndex, attachmentName);
				if (attachment == NULL) {
					for (int j = 0; j < timelines->size; ++j) {
						spTimeline_dispose(timelines->items[j]);
					}
					spTimelineArray_dispose(timelines);
					spSkeletonBinary_setError_(self, "Attachment not found: ", attachmentName);
					FREE(attachmentName);
					return NULL;
				}
				FREE(attachmentName);

				int vertexCount = 0;
				if (attachment->type == SP_ATTACHMENT_MESH) {
					vertexCount = SUB_CAST(spMeshAttachment, attachment)->verticesCount;
				}
				else if (attachment->type == SP_ATTACHMENT_SKINNED_MESH) {
					vertexCount = SUB_CAST(spSkinnedMeshAttachment, attachment)->weightsCount / 3 * 2;
				}
				spFFDTimeline* timeline = spFFDTimeline_create(frameCount, vertexCount);
				timeline->slotIndex = slotIndex;
				timeline->attachment = attachment;

				float* tempVertices = CALLOC(float, vertexCount);

				for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
					float time = readFloat(input);
					float* frameVertices = NULL;
					int end = readVarint(input, 1);
					if (end == 0) {
						if (attachment->type == SP_ATTACHMENT_MESH) {
							frameVertices = SUB_CAST(spMeshAttachment, attachment)->vertices;
						}
						else {
							frameVertices = tempVertices;
						}
					}
					else {
						frameVertices = tempVertices;
						int start = readVarint(input, 1);
						end += start;
						for (int j = start; j < end; ++j) {
							frameVertices[j] = readFloat(input) * self->scale;
						}
						if (attachment->type == SP_ATTACHMENT_MESH) {
							float* meshVertices = SUB_CAST(spMeshAttachment, attachment)->vertices;
							for (int j = 0; j < vertexCount; ++j) {
								frameVertices[j] += meshVertices[j];
							}
						}

					}
					spFFDTimeline_setFrame(timeline, frameIndex, time, frameVertices);
					if (frameIndex < frameCount - 1) readCurve(input, SUPER(timeline), frameIndex);
				}

				FREE(tempVertices);

				spTimelineArray_add(timelines, SUPER(SUPER(timeline)));
				duration = MAX(duration, timeline->frames[frameCount - 1]);
			}
		}
	}

	/* Draw order timeline. */
	int drawOrderCount = readVarint(input, 1);
	if (drawOrderCount > 0) {
		spDrawOrderTimeline* timeline = spDrawOrderTimeline_create(drawOrderCount, skeletonData->slotsCount);
		for (int i = 0; i < drawOrderCount; ++i) {
			int offsetCount = readVarint(input, 1);
			int* unchanged = MALLOC(int, skeletonData->slotsCount - offsetCount);
			int* drawOrder = MALLOC(int, skeletonData->slotsCount);
			memset(drawOrder, -1, skeletonData->slotsCount * sizeof(int));

			int originalIndex = 0, unchangedIndex = 0;
			for (int ii = 0; ii < offsetCount; ++ii) {
				int slotIndex = readVarint(input, 1);
				/* Collect unchanged items. */
				while (originalIndex != slotIndex) {
					unchanged[unchangedIndex++] = originalIndex++;
				}
				/* Set changed items. */
				drawOrder[originalIndex + readVarint(input, 1)] = originalIndex;
				++originalIndex;
			}
			/* Collect remaining unchanged items. */
			while (originalIndex < skeletonData->slotsCount) {
				unchanged[unchangedIndex++] = originalIndex++;
			}
			/* Fill in unchanged items. */
			for (int ii = skeletonData->slotsCount - 1; ii >= 0; ii--) {
				if (drawOrder[ii] == -1) drawOrder[ii] = unchanged[--unchangedIndex];
			}

			FREE(unchanged);
			float time = readFloat(input);
			spDrawOrderTimeline_setFrame(timeline, i, time, drawOrder);
			FREE(drawOrder);
		}
		spTimelineArray_add(timelines, SUPER(timeline));
		duration = MAX(duration, timeline->frames[drawOrderCount - 1]);
	}

	/* Event timeline. */
	int eventCount = readVarint(input, 1);
	if (eventCount > 0) {
		spEventTimeline* timeline = spEventTimeline_create(eventCount);
		for (int i = 0; i < eventCount; ++i) {
			float time = readFloat(input);
			spEventData* eventData = skeletonData->events[readVarint(input, 1)];
			spEvent* event = spEvent_create(eventData);
			event->intValue = readVarint(input, 0);
			event->floatValue = readFloat(input);
			if (readBoolean(input)) {
				event->stringValue = readString(input);
			}
			else {
				MALLOC_STR(event->stringValue, eventData->stringValue);
			}
			spEventTimeline_setFrame(timeline, i, time, event);
		}
		spTimelineArray_add(timelines, SUPER(timeline));
		duration = MAX(duration, timeline->frames[drawOrderCount - 1]);
	}

	spAnimation* animation = spAnimation_create(name, timelines->size);
	animation->duration = duration;
	animation->timelines = timelines->items;

	return animation;
}

/*====================  end of internal class functions  ====================*/

/*====================  public class functions  ====================*/

spSkeletonBinary* spSkeletonBinary_createWithLoader(spAttachmentLoader* attachmentLoader) {
	spSkeletonBinary* self = SUPER(NEW(_spSkeletonBinary));
	self->scale = 1;
	self->attachmentLoader = attachmentLoader;
	return self;
}

spSkeletonBinary* spSkeletonBinary_create(spAtlas* atlas) {
	spAtlasAttachmentLoader* attachmentLoader = spAtlasAttachmentLoader_create(atlas);
	spSkeletonBinary* self = spSkeletonBinary_createWithLoader(SUPER(attachmentLoader));
	SUB_CAST(_spSkeletonBinary, self)->ownsLoader = 1;
	return self;
}

void spSkeletonBinary_dispose(spSkeletonBinary* self) {
	_spSkeletonBinary* internal = SUB_CAST(_spSkeletonBinary, self);
	if (internal->ownsLoader) spAttachmentLoader_dispose(self->attachmentLoader);
	FREE(self->error);
	FREE(self);
}

spSkeletonData* spSkeletonBinary_readSkeletonDataFile(spSkeletonBinary* self, const char* path) {
	int length;
	spSkeletonData* skeletonData;
	const char* binary = _spUtil_readFile(path, &length);
	if (length == 0 || !binary) {
		spSkeletonBinary_setError_(self, "Unable to read skeleton file: ", path);
		return NULL;
	}
	skeletonData = spSkeletonBinary_readSkeletonData(self, (unsigned char*)binary, length);
	FREE(binary);
	return skeletonData;
}

spSkeletonData* spSkeletonBinary_readSkeletonData(spSkeletonBinary* self, const unsigned char* binary, const int length) {
	int i, ii, nonessential;

	spSkeletonData* skeletonData;
	_spSkeletonBinary* internal = SUB_CAST(_spSkeletonBinary, self);

	_dataInput* input = NEW(_dataInput);
	input->cursor = binary;
	input->end = binary + length;

	FREE(self->error);
	CONST_CAST(char*, self->error) = 0;

	skeletonData = spSkeletonData_create();

	skeletonData->hash = readString(input);
	if (!strlen(skeletonData->hash)) {
		FREE(skeletonData->hash);
		skeletonData->hash = 0;
	}

	skeletonData->version = readString(input);
	if (!strlen(skeletonData->version)) {
		FREE(skeletonData->version);
		skeletonData->version = 0;
	}

	skeletonData->width = readFloat(input);
	skeletonData->height = readFloat(input);

	nonessential = readBoolean(input);
	if (nonessential) {
		/*CS runtime has SkeletonData.imagesPath, but not C*/
		FREE(readString(input));
	}

	/* Bones. */
	skeletonData->bonesCount = readVarint(input, 1);
	skeletonData->bones = MALLOC(spBoneData*, skeletonData->bonesCount);
	for (i = 0; i < skeletonData->bonesCount; ++i) {

		const char* name = readString(input);
		int parentIndex = readVarint(input, 1) - 1;
		spBoneData* parent = i == 0 ? NULL : skeletonData->bones[parentIndex];
		spBoneData* boneData = spBoneData_create(name, parent);
		FREE(name);
		boneData->x = readFloat(input) * self->scale;
		boneData->y = readFloat(input) * self->scale;
		boneData->scaleX = readFloat(input);
		boneData->scaleY = readFloat(input);
		boneData->rotation = readFloat(input);
		boneData->length = readFloat(input) * self->scale;
		boneData->flipX = readBoolean(input);
		boneData->flipY = readBoolean(input);
		boneData->inheritScale = readBoolean(input);
		boneData->inheritRotation = readBoolean(input);

		if (nonessential) {
			int colour = readInt(input); /*skip bone colour*/
		}
		skeletonData->bones[i] = boneData;
	}

	/* IK constraints. */
	skeletonData->ikConstraintsCount = readVarint(input, 1);
	skeletonData->ikConstraints = MALLOC(spIkConstraintData*, skeletonData->ikConstraintsCount);
	for (i = 0; i < skeletonData->ikConstraintsCount; ++i) {
		const char* name = readString(input);

		spIkConstraintData* ikConstraintsData = spIkConstraintData_create(name);
		FREE(name);

		ikConstraintsData->bonesCount = readVarint(input, 1);
		ikConstraintsData->bones = MALLOC(spBoneData*, ikConstraintsData->bonesCount);
		for (ii = 0; ii < ikConstraintsData->bonesCount; ++ii) {
			ikConstraintsData->bones[ii] = skeletonData->bones[readVarint(input, 1)];
		}
			
		ikConstraintsData->target = skeletonData->bones[readVarint(input, 1)];
		ikConstraintsData->mix = readFloat(input);
		ikConstraintsData->bendDirection = readSByte(input);
		skeletonData->ikConstraints[i] = ikConstraintsData;
	}

	/* Slots. */
	skeletonData->slotsCount = readVarint(input, 1);
	skeletonData->slots = MALLOC(spSlotData*, skeletonData->slotsCount);
	for (i = 0; i < skeletonData->slotsCount; ++i) {
		const char* slotName = readString(input);
		spBoneData* boneData = skeletonData->bones[readVarint(input, 1)];

		spSlotData* slotData = spSlotData_create(slotName, boneData);
		FREE(slotName);
		readColor(input, &slotData->r, &slotData->g, &slotData->b, &slotData->a);
		slotData->attachmentName = readString(input);
		slotData->additiveBlending = readBoolean(input);
		skeletonData->slots[i] = slotData;
	}

	/* Default skin. */
	skeletonData->defaultSkin = spSkeletonBinary_readSkin_(self, input, "default", skeletonData, nonessential);
	if (self->attachmentLoader->error1) {
		spSkeletonData_dispose(skeletonData);
		spSkeletonBinary_setError_(self, self->attachmentLoader->error1, self->attachmentLoader->error2);
		return NULL;
	}

	skeletonData->skinsCount = readVarint(input, 1);
	if (skeletonData->defaultSkin) {
		++skeletonData->skinsCount;
	}

	/* Skins. */
	skeletonData->skins = MALLOC(spSkin*, skeletonData->skinsCount);
	if (skeletonData->defaultSkin) {
		skeletonData->skins[0] = skeletonData->defaultSkin;
	}

	for (i = skeletonData->defaultSkin ? 1 : 0; i < skeletonData->skinsCount; ++i) {
		const char* skinName = readString(input);
		spSkin* skin = spSkeletonBinary_readSkin_(self, input, skinName, skeletonData, nonessential);
		FREE(skinName);
		if (self->attachmentLoader->error1) {
			spSkeletonData_dispose(skeletonData);
			spSkeletonBinary_setError_(self, self->attachmentLoader->error1, self->attachmentLoader->error2);
			return NULL;
		}
		skeletonData->skins[i] = skin;
	}

	/* Events. */
	skeletonData->eventsCount = readVarint(input, 1);
	skeletonData->events = MALLOC(spEventData*, skeletonData->eventsCount);
	for (i = 0; i < skeletonData->eventsCount; ++i) {
		const char* name = readString(input);
		spEventData* eventData = spEventData_create(name);
		FREE(name);
		eventData->intValue = readVarint(input, 0);
		eventData->floatValue = readFloat(input);
		eventData->stringValue = readString(input);
		skeletonData->events[i] = eventData;
	}

	/* Animations. */
	skeletonData->animationsCount = readVarint(input, 1);
	skeletonData->animations = MALLOC(spAnimation*, skeletonData->animationsCount);
	for (i = 0; i < skeletonData->animationsCount; ++i) {
		const char* name = readString(input);
		spAnimation* animation = spSkeletonBinary_readAnimation_(self, name, input, skeletonData);
		FREE(name);
		if (!animation) {
			FREE(input);
			/*the remaining still not allocated.*/
			skeletonData->animationsCount = i;
			spSkeletonData_dispose(skeletonData);
			spSkeletonBinary_setError_(self, "Animation corrupted: ", name);
			return NULL;
		}
		skeletonData->animations[i] = animation;
	}

	FREE(input);
	return skeletonData;
}
