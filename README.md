# spine-c-2.1.27
Binary skeleton reader written in C for spine 2.1.27 runtime.  
The files are intended to be used with spine-c runtime in official's [2.1.25](https://github.com/EsotericSoftware/spine-runtimes/tree/2.1.25) tag.

The repository contains: 
- `SkeletonBinary.c`
  - Binary skeleton reader utilising macro defined in `Array.h` and `extension.h`.
  - The code is based on `SkeletonBinary.cs` in 2.1.25
- `Array.c`
  - C vector backported from spine-c 4.1.
  - `dll.h` is necessary to be consistent with declaration rule of the later version. 
- `extension.c`
  - Some external functions which lack `sp` prefix have been renamed so as to be consistent with spine-c 3.6 and later.
  - `MAX`, `MIN` have been added for C vector, and `UNUSED` has been added for `_spReadFile()`.

Plus, the files which are to be overwritten to those of 2.1.25 tag are contained.
- `spine.h`
  - A line `#include <spine/SkeletonBinary.h>` is added.
- `Bone.c`
  - Fix on matrix initialisation is backported.

## Note on backport

### `Bone.c`

Fix on [spBone_create()](https://github.com/EsotericSoftware/spine-runtimes/issues/924)
 - the matrix `a` and `d` are `m00` and `m11` in spine 2.1.25.

```
spBone* spBone_create (spBoneData* data, spSkeleton* skeleton, spBone* parent) {
	spBone* self = NEW(spBone);
	CONST_CAST(spBoneData*, self->data) = data;
	CONST_CAST(spSkeleton*, self->skeleton) = skeleton;
	CONST_CAST(spBone*, self->parent) = parent;
	CONST_CAST(float, self->m00) = 1.0f;
	CONST_CAST(float, self->m11) = 1.0f;
	spBone_setToSetupPose(self);
	return self;
}
```
