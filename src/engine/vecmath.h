#pragma once
// Matemática vetorial da EuroLand (módulos Vec, Mat, Quat, RotMatrix e PS2Sinf do ACTION.ELF).
// Assinaturas iguais às originais (ver o mangling em out/functions.tsv); a ordem dos
// parâmetros segue o jogo, inclusive onde destino vem antes da origem.

#include "engine/ps2types.h"

// ---- constantes (preenchidas por bootup_bootup no PS2) ----
extern const _VECTOR CONSTANT_ZERO_VECTOR;
extern const _VECTOR CONSTANT_UP1;   // (0, 1, 0): "para cima" padrão
extern const _VECTOR CONSTANT_UP2;   // (1, 0, 0): alternativo quando a direção é quase vertical
extern const MATRIX CONSTANT_MAT_IDENT;

// ---- seno e arco-tangente aproximados ----
float ATAN2_APPROX(float y, float x);

// ---- seno da VU0 (plataforma, reimplementado com o mesmo polinômio) ----
float PS2Sinf(float a);
void PS2Sinf2(float a, float b, float* sa, float* sb);
void PS2Sinf3(float a, float b, float c, float* sa, float* sb, float* sc);

// ---- vetores ----
float Vec_AngleDifference(float a, float b);
// Gira o ângulo `cur` em direção a `target` (radianos): o caminho mais curto, no máximo `max_step`, e só a fração
// `rate` dele; perto de target (0,0001) dá o próprio target. O resultado parte de cur sem normalizar.
float Ang_RotateToBearing(float cur, float target, float rate, float max_step);
void Vec_Copy2(const _VECTOR* src, _VECTOR* dst1, _VECTOR* dst2);
void Vec_UnitVector(_VECTOR* out, const _VECTOR* from, const _VECTOR* to);
void Vec_Zero(_VECTOR* v);
void Vec_Swap(_VECTOR* a, _VECTOR* b);
void Vec_Avg3(const _VECTOR* a, const _VECTOR* b, const _VECTOR* c, _VECTOR* out);
void Vec_Avg2(const _VECTOR* a, const _VECTOR* b, _VECTOR* out);
float Vec_Dist3D(const _VECTOR* a, const _VECTOR* b);
float Vec_SqDist3D(const _VECTOR* a, const _VECTOR* b);
float Vec_Dist2D(const _VECTOR* a, const _VECTOR* b);
float Vec_SqDist2D(const _VECTOR* a, const _VECTOR* b);
float Vec_Magnitude(const _VECTOR* v);
float Vec_SqMagnitude(const _VECTOR* v);
float Vec_NormaliseLen(_VECTOR* out, const _VECTOR* in);
void Vec_Normalise(_VECTOR* out, const _VECTOR* in);
void Vec_CrossNormalise(const _VECTOR* a, const _VECTOR* b, _VECTOR* out);
float Vec_ScalarTripleProduct(const _VECTOR* a, const _VECTOR* b, const _VECTOR* c);
void Vec_NormToRot(const _VECTOR* normal, _VECTOR* rot);
void Vec_DirToRot(const _VECTOR* dir, _VECTOR* rot);
void Vec_Mul(_VECTOR* v, const _VECTOR* s);
void Vec_MakeOrthonormalBasis(_VECTOR* u, _VECTOR* v, _VECTOR* n);
void Vec_Max(const _VECTOR* a, const _VECTOR* b, _VECTOR* out);
void Vec_Min(const _VECTOR* a, const _VECTOR* b, _VECTOR* out);
void Vec_MinMax(const _VECTOR* a, const _VECTOR* b, _VECTOR* out_min, _VECTOR* out_max);
void Vec_MinMax3(const _VECTOR* a, const _VECTOR* b, const _VECTOR* c, _VECTOR* out_min, _VECTOR* out_max);
bool Vec_IsEqual(const _VECTOR* a, const _VECTOR* b, float tolerance);
void Vec_Negate(const _VECTOR* in, _VECTOR* out);
void Vec_MulR32(_VECTOR* out, const _VECTOR* in, float s);
void Vec_MulR32(_VECTOR4* out, const _VECTOR4* in, float s);
void Vec_MulR32(_VECTOR* out, const _VECTOR* in, float sx, float sy, float sz);
void Vec_MatToEuler(_VECTOR* out, const MATRIX* m, int order);
void Vec_Track(float t, _VECTOR* a, _VECTOR* b, MATRIX* m, quaternion_tag* q);
bool Vec_LineSphereIntersection(const _VECTOR* a, const _VECTOR* b, const _VECTOR* center, float radius,
                                _VECTOR* out);
bool Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM(const _VECTOR* a, const _VECTOR* b, const _VECTOR* center,
                                                       float radius, _VECTOR* out);
void Vec_Spherical_2_Cartesian(_VECTOR* out, float radius, float a, float b);
void Vec_Cartesian_2_Spherical(_VECTOR* out, float x, float y, float z);
void vecutil_cartesian_to_spherical_acc(_VECTOR* out, float x, float y, float z);

// ---- matrizes ----
void Mat_Copy(const MATRIX* src, MATRIX* dst);
void Mat_CopyRot(const MATRIX* src, MATRIX* dst);
void Mat_GetDir(_VECTOR* out, MATRIX* m);
void Mat_GetUp(_VECTOR* out, MATRIX* m);
void Mat_GetNorm(_VECTOR* out, MATRIX* m);
void Mat_GetTrans(_VECTOR* out, MATRIX* m);
void Mat_SetDir(_VECTOR* v, MATRIX* m);
void Mat_SetUp(_VECTOR* v, MATRIX* m);
void Mat_SetNorm(_VECTOR* v, MATRIX* m);
void Mat_SetTrans(_VECTOR* v, MATRIX* m);
void Mat_Identity(MATRIX* m);
void Mat_IdentityT(MATRIX* m);
// out(3x3) = b(3x3) * a(3x3); a linha 3 e a coluna w de out ficam como estão.
void MulMatrix0(const MATRIX* a, const MATRIX* b, MATRIX* out);
// b(3x3) = b(3x3) * a(3x3) no lugar; a linha 3 e a coluna w de b ficam como estão.
void MulMatrix2(const MATRIX* a, MATRIX* b);
void Mat_Inverse(MATRIX* m);
void Mat_Inverse(const MATRIX* in, MATRIX* out);
void Mat_Scale3f(MATRIX* out, MATRIX* in, float sx, float sy, float sz);
void MatrixScale(MATRIX* m, float s);
void Mat_World2ViewMat(MATRIX* world, MATRIX* view);
void Mat_Rotate(_VECTOR* euler, MATRIX* m);
void Mat_LookAt(MATRIX* m, const _VECTOR* eye, const _VECTOR* target, const _VECTOR* up, const _VECTOR* alt_up);
void Mat_Align2Dir(MATRIX* m, const _VECTOR* dir, const _VECTOR* up, const _VECTOR* alt_up);
void Mat_Align2Up(MATRIX* m, const _VECTOR* up, const _VECTOR* dir);
void Mat_Normalize(MATRIX* m);

void RotMatrix(const _VECTOR* euler, MATRIX* m);
void RotTransMatrix(const _VECTOR* euler, const _VECTOR* trans, MATRIX* m);
void RotMatrixZYX(const _VECTOR* euler, MATRIX* m);
void RotMatrixX(float a, MATRIX* m);
void RotMatrixY(float a, MATRIX* m);
void RotMatrixZ(float a, MATRIX* m);
void RotMatrixXY(float ax, float ay, MATRIX* m);
void RotTransMat(const MATRIX* a, MATRIX* b);
void RotPreTransVec(const MATRIX* m, const _VECTOR* in, _VECTOR* out);

// ---- quatérnios ----
bool Quat_IsEqual(const quaternion_tag* a, const quaternion_tag* b, float tolerance);
void Quat_Copy(quaternion_tag* dst, const quaternion_tag* src);
void Quat_QuaternionMultiply(const quaternion_tag* a, const quaternion_tag* b, quaternion_tag* out);
void Quat_Rot_Vec(const quaternion_tag* q, const _VECTOR* in, _VECTOR* out);
void Quat_EulerToQuat(const _VECTOR* euler, quaternion_tag* q);
void Quat_QuatToMat(const quaternion_tag* q, MATRIX* m);
void Quat_AxisAngleToQuat(const quaternion_tag* axis_angle, quaternion_tag* q);
void Quat_QuatTransToMat(const quaternion_tag* q, const _VECTOR* trans, MATRIX* m);
void Quat_Normalise(quaternion_tag* q);
void Quat_Slerp(float t, const quaternion_tag* a, const quaternion_tag* b, quaternion_tag* out);
void Quat_Slerp_Acc(float t, const quaternion_tag* a, const quaternion_tag* b, quaternion_tag* out);
void Quat_QuaternionToMatrix(MATRIX* m, const quaternion_tag* q);
void Quat_MatToQuat(quaternion_tag* q, const MATRIX* m);
void Quat_MatrixToQuaternion(quaternion_tag* q, const MATRIX* m);
