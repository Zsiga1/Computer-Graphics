#include <math.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <GLUT/glut.h>
#else
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#endif

#define RND (2.0*(double)rand()/RAND_MAX-1.0)
#define PI 3.14159265f
#define depth_Max 5
#define epsilon 1e-3f
#define photonConst 3.0f


const int screenWidth = 600;
const int screenHeight = 600;



//--------------------------------------------------------
// 3D Vektor
//--------------------------------------------------------
struct Vector {
	float x, y, z;

	Vector() {
		x = y = z = 0;
	}
	Vector(float x0, float y0, float z0 = 0) {
		x = x0; y = y0; z = z0;
	}
	Vector operator*(float a) {
		return Vector(x * a, y * a, z * a);
	}
	Vector operator+(const Vector& v) {
		return Vector(x + v.x, y + v.y, z + v.z);
	}
	Vector operator/(float a) {
		return Vector(x / a, y / a, z / a);
	}
	Vector operator-(const Vector& v) {
		return Vector(x - v.x, y - v.y, z - v.z);
	}
	float operator*(const Vector& v) { 	// dot product
		return (x * v.x + y * v.y + z * v.z);
	}
	Vector operator%(const Vector& v) { 	// cross product
		return Vector(y*v.z - z*v.y, z*v.x - x*v.z, x*v.y - y*v.x);
	}
	float Length() { return sqrt(x * x + y * y + z * z); }

	void Normalize()
	{
		float length = Length();
		x /= length;
		y /= length;
		z /= length;
	}
};

Vector UpVector = Vector(0.0f, 1.0f, 0.0f);

//--------------------------------------------------------
// Spektrum illetve szin
//--------------------------------------------------------
struct Color {
	float r, g, b;

	Color() {
		r = g = b = 0;
	}
	Color(float r0, float g0, float b0) {
		r = r0; g = g0; b = b0;
	}
	Color operator*(float a) {
		return Color(r * a, g * a, b * a);
	}
	Color operator/(float a) {
		return Color(r / a, g / a, b / a);
	}
	Color operator*(const Color& c) {
		return Color(r * c.r, g * c.g, b * c.b);
	}
	Color operator/(const Color& c) {
		return Color(r / c.r, g / c.g, b / c.b);
	}
	Color operator+(const Color& c) {
		return Color(r + c.r, g + c.g, b + c.b);
	}
	Color operator-(const Color& c) {
		return Color(r - c.r, g - c.g, b - c.b);
	}

	bool operator!=(const Color& c) {
	    if (r!=c.r || g!=c.g || b!=c.b) {
            return true;
	    } else {
            return false;
	    }
	}
};

float image[screenWidth*screenHeight * 3];

struct Ray
{
	Vector rOrigo, rDirection;
};

struct ObjMat
{
	Color F0;
	float N;
	float shininess;
	Color ka_AmbientColor;
	Color kd_DiffuseColor;
	Color ks_SpecularColor;
	bool IsReflective;
	bool IsRefractive;

	bool flat;

	ObjMat()
	{
		IsReflective = false;
		IsRefractive = false;
		N = 1.0f;
		shininess = 0.0f;

		flat = false;
	}

	void DirOfReflection(Vector& Reflected, Vector SurfaceNormal, Vector Incoming)
	{
		float cosa = (Incoming*SurfaceNormal) * -1.0f;
		Reflected = Incoming + SurfaceNormal * cosa * 2.0f;
	}

	bool DirOfRefraction(Vector& Refracted, Vector SurfaceNormal, Vector Incoming)
	{
		float cosa = -(SurfaceNormal * Incoming);
		float cn = N;

		if (cosa < 0.0f)
		{
			cosa = -cosa;
			SurfaceNormal = SurfaceNormal * (-1.0f);
			cn = 1.0f / N;
		}

		float disc = 1.0f - (1.0f - cosa * cosa) / cn / cn;

		if (disc < 0.0f)
		{
			return false;
		}

		Refracted = Incoming / cn + SurfaceNormal * (cosa / cn - sqrt(disc));

		return true;
	}

	Color CalculateFresnel(Vector SurfaceNormal, Vector Incoming)
	{
		float cosa = fabs(SurfaceNormal * Incoming);
		return F0 + (Color(1.0f, 1.0f, 1.0f) - F0) * pow(1 - cosa, 5);
	}

	Color ReflectionRadiance(Vector L, Vector SurfaceNormal, Vector Incoming, Color LinearLight)
	{
		float costetha = SurfaceNormal * Incoming;
		if (costetha < 0.0f)
		{
			Color(0.0f, 0.0f, 0.0f);
		}

		Color ReflectedColor = LinearLight * kd_DiffuseColor * costetha;

		Vector H = L + Incoming;
		H.Normalize();

		float cosdelta = SurfaceNormal * H;
		if (cosdelta < 0.0f)
		{
			return ReflectedColor;
		}

		ReflectedColor = ReflectedColor + LinearLight * ks_SpecularColor * pow(cosdelta, shininess);

		return ReflectedColor;
	}

	void SetF0(Color n, Color k)
	{
		F0 = ((n - Color(1.0f, 1.0f, 1.0f))*(n - Color(1.0f, 1.0f, 1.0f)) + k*k) / ((n + Color(1.0f, 1.0f, 1.0f))*(n + Color(1.0f, 1.0f, 1.0f)) + k*k);
		N = n.r;
	}
};

struct Collide
{
	Vector position;
	Vector normalV;
	ObjMat material;
	float t;

	Collide()
	{
		t = -1.0f;
	}
};

class Object
{
protected:
	ObjMat Material;
public:
	virtual Collide Intersect(Ray ray) = 0;

};

class Paraboloid : public Object
{
	Vector R0;
	Vector Axis;
	float Height;
public:
	Paraboloid(ObjMat material, Vector r0, Vector axis, float height)
	{
		Material = material;

		R0 = r0;

		Axis = axis;
		Axis.Normalize();

		Height = height;

	};

	Collide Intersect(Ray ray)
	{
		float Radius = ray.rDirection * Axis / 4;

		Collide collide;
		Vector ray_from_R0 = ray.rOrigo - R0;

		double a = ray.rDirection * ray.rDirection - (ray.rDirection * Axis) * (ray.rDirection * Axis);
		double b = (ray_from_R0 * ray.rDirection - (Axis * ray_from_R0) * (Axis * ray.rDirection)) * 2.0f;
		double c = ray_from_R0*ray_from_R0 - Radius - (Axis * ray_from_R0) * (Axis * ray_from_R0);

		double Discriminant = b * b - 4.0f * a * c;

		if (Discriminant < 0.0f)
		{
			return collide;
		}

		Discriminant = sqrt(Discriminant);
		collide.t = (-b - Discriminant) / (2.0f * a);

		if (collide.t < 0.0f)
		{
			collide.t = (-b + Discriminant) / (2.0f * a);
		}

		if (collide.t < epsilon)
		{
			collide.t = -1.0f;
			return collide;
		}

		collide.position = ray.rOrigo + ray.rDirection * collide.t / 1;
		float hp = (collide.position - R0) * Axis;

		if (hp > 0.0f && hp < Height)
		{
			collide.normalV = collide.position - (R0 + Axis * hp);
			collide.normalV.Normalize();
			collide.material = Material;
		}
		else
		{
			if (hp > Height || hp < 0.0f)
			{
				collide.t = -1.0f;
			}
		}

		return collide;
	}
};

class Cylinder : public Object
{
	Vector R0;
	Vector Axis;
	float Height;
	float Radius;

	ObjMat bottomCapMaterial;
public:
	Cylinder(ObjMat material, ObjMat capMat, Vector r0, Vector axis, float height, float radius)
	{
		Material = material;
		bottomCapMaterial = capMat;
		bottomCapMaterial.flat = true;

		R0 = r0;

		Axis = axis;
		Axis.Normalize();

		Height = height;
		Radius = radius;
	};

	Collide Intersect(Ray ray)
	{
		Collide collide;
		Vector ray_from_R0 = ray.rOrigo - R0;

		double a = ray.rDirection * ray.rDirection - (ray.rDirection * Axis) * (ray.rDirection * Axis);
		double b = (ray_from_R0 * ray.rDirection - (Axis * ray_from_R0)* (Axis * ray.rDirection)) * 2.0f ;
		double c = ray_from_R0*ray_from_R0 - Radius * Radius - (Axis * ray_from_R0) * (Axis * ray_from_R0);

		double Discriminant = b * b - 4.0f * a * c;

		if (Discriminant < 0.0f)
		{
			return collide;
		}

		Discriminant = sqrt(Discriminant);
		collide.t = (-b - Discriminant) / (2.0f * a);

		if (collide.t < 0.0f || fabs(collide.t) < epsilon)
		{
			collide.t = (-b + Discriminant) / (2.0f * a);
		}

		if (collide.t < epsilon)
		{
			collide.t = -1.0f;


			return collide;
		}

		collide.position = ray.rOrigo + ray.rDirection * collide.t;
		float hp = (collide.position - R0) * Axis;

		if (hp > 0.0f && hp < Height)
		{
			collide.normalV = collide.position - (R0 + Axis * hp);
			collide.normalV.Normalize();
			collide.material = Material;
		}
		else
		{
			if (hp > Height)
			{
				collide.t = -1.0f;
			}

			if (hp < 0.0f)
			{
				Vector normalVector = Vector(0.0f, 1.0f, 0.0f);

				collide.t = -(normalVector* ray.rOrigo) / (normalVector* ray.rDirection);
				collide.position = ray.rOrigo + ray.rDirection * collide.t;
				collide.normalV = UpVector;
				collide.material = bottomCapMaterial;
			}
		}

		return collide;
	}
};

class Camera
{
	Vector DirVictorUp;
	Vector DirVictorRight;
	Vector LookAtPosition;
	Vector eye_Position;
public:

	Camera(){};

	Camera(Vector eyePos, Vector lookatPos, Vector rightVector, Vector upVector)
	{
		eye_Position = eyePos;
		LookAtPosition = lookatPos;
		DirVictorRight = rightVector;
		DirVictorUp = upVector;
	}

	Ray GetRay(float screenX, float screenY)
	{
		Ray ray_return;
		ray_return.rOrigo = eye_Position;

		ray_return.rDirection = LookAtPosition + DirVictorRight * (2.0f * (screenX+0.5) / screenWidth - 1.0f) + DirVictorUp * (2.0f * (screenY+0.5) / screenHeight - 1.0f) - eye_Position;
		ray_return.rDirection.Normalize();

		return ray_return;
	}
};

class Light
{
public:
	Vector SourcePosition;
	Color Power;

	Light(Vector pos = Vector(), Color power = Color())
	{
		SourcePosition = pos;
		Power = power;
	};

	Vector GetDirection(Vector p)
	{
		return (SourcePosition - p) / (SourcePosition - p).Length();
	}

	float GetDistance(Vector p)
	{
		return (SourcePosition - p).Length();
	}

	Color LinLightIntesity(Vector p)
	{
		return Power / ((SourcePosition - p)*(SourcePosition - p) * 4.0f * PI);
	}
};

Color Pattern(const float x, const float y, const float z)
{
    if (fmod(fabs(x), 0.5)<0.25) {
        if (fmod(fabs(z), 0.5)<0.25) {
            return Color(0.0f, 0.0f, 0.0f);
        } else {
            return Color(1.0f, 1.0f, 1.0f);
        }
    } else {
        if (fmod(fabs(z), 0.5)<0.25) {
            return Color(1.0f, 1.0f, 1.0f);
        } else {
            return Color(0.0f, 0.0f, 0.0f);
        }
    }
}


class World
{
	Color La_AmbientLight;
	Color SkyColor;
	Object* objects[11];
	int objectCount;
	Camera* camera;
	Light lights[4];
	int lightCount;

	Color PMap[1000][1000];

	int MapSize;
	int PhotonsToShoot;
public:
	World()
	{
		objectCount = 0;
		lightCount = 0;

		MapSize = 1000;
		PhotonsToShoot = 1e4;
	}

	Color RayTrace(Ray ray, int depth = 0)
	{
        int i=0, x=-5, y=-5;
		if (depth > depth_Max)
		{
			return La_AmbientLight;
		}

		Collide collide = IntersectWorld(ray);
		Vector normal = collide.normalV;

		if (collide.t < 0.0f)
		{
			return SkyColor;
		}

        Color c;

		if (collide.material.kd_DiffuseColor!=Color(0.0f, 0.0f, 0.0f)) {
            c = La_AmbientLight * Pattern(collide.position.x, collide.position.y, collide.position.z);
		} else {
            c = La_AmbientLight * collide.material.ka_AmbientColor;
		}

        while (i<lightCount) {
            Ray shadowRay;
			shadowRay.rOrigo = collide.position;
			shadowRay.rDirection = lights[i].GetDirection(collide.position);

			Collide shadowCollide = IntersectWorld(shadowRay);
			if (shadowCollide.t < 0.0f || (collide.position - shadowCollide.position).Length() > lights[i].GetDistance(collide.position))
			{
				c = c + collide.material.ReflectionRadiance(lights[i].GetDirection(collide.position), collide.normalV, ray.rDirection*(-1.0f), lights[i].LinLightIntesity(collide.position));
                while (x<6) {
                        while (y<6) {
                        int indexX = int(collide.position.x / photonConst * (MapSize / 2) + (MapSize / 2)) + x;
						int indexY = int(collide.position.z / photonConst * (MapSize / 2) + (MapSize / 2)) + y;

						if (indexX > 0 && indexX < MapSize && indexY > 0 && indexY < MapSize)
						{
							Vector center = Vector(indexX, indexY);
							Vector tmp = Vector(indexX - x, indexY - y);
							if ((center - tmp).Length() < 5)
							{
								c = c * (Color(1, 1, 1) + PMap[indexX][indexY]);
							}
						}
                            y++;
                        }
                    x++;
                }
			}
            i++;
        }

		if (collide.material.IsReflective == true)
		{
			Ray reflectionRay;
			reflectionRay.rOrigo = collide.position;
			collide.material.DirOfReflection(reflectionRay.rDirection, collide.normalV, ray.rDirection);

			c = c + collide.material.CalculateFresnel(collide.normalV, ray.rDirection)* RayTrace(reflectionRay, depth + 1);
		}

		if (collide.material.IsRefractive == true)
		{
			Ray refractedRay;
			refractedRay.rOrigo = collide.position;
			collide.material.DirOfRefraction(refractedRay.rDirection, collide.normalV, ray.rDirection);

			c = c + (Color(1.0f,1.0f,1.0f)-collide.material.CalculateFresnel(collide.normalV, ray.rDirection))* RayTrace(refractedRay, depth + 1);
		}
		return c;
	}

	void Build()
	{
        Vector up(0.0f, -1.0f, 0.0f);
	    Vector eyePos(0.0f, 0.4f, -1.0f);
	    Vector lookAt(0.0f, -0.1f, 0.0f);
	    Vector dir =lookAt-eyePos;
	    dir.Normalize();
	    Vector right = dir%up;
	    right.Normalize();
	    up=dir%right;
	    up.Normalize();
		camera = new Camera(eyePos, lookAt, right, up);

		ObjMat brawnMaterial;
		brawnMaterial.kd_DiffuseColor = Color(0.4f, 0.2f, 0.0f);
		brawnMaterial.ka_AmbientColor = Color(0.4f, 0.2f, 0.0f);
		brawnMaterial.ks_SpecularColor = Color(8.0f, 8.0f, 8.0f);
		brawnMaterial.shininess = 80;

		ObjMat goldMaterial;
		goldMaterial.ka_AmbientColor = Color(0.192f, 0.192f, 0.192f);
		goldMaterial.IsReflective = true;
		goldMaterial.SetF0(Color(0.17f, 0.35f, 1.5f), Color(3.1f, 2.7f, 1.9f));//Arany (n/k).....0.17/3.1, 0.35/2.7, 1.5/1.9

		ObjMat glassMaterial;
		glassMaterial.IsRefractive = true;
		glassMaterial.SetF0(Color(1.5f, 1.5f, 1.5f), Color(0.0f, 0.0f, 0.0f)); //Üveg (n/k)......1.5/0.0, 1.5/0.0, 1.5/0.0

		ObjMat silverMaterial;
		silverMaterial.ka_AmbientColor = Color(0.192f, 0.192f, 0.192f);
		silverMaterial.IsReflective = true;
		silverMaterial.SetF0(Color(0.14f, 0.16f, 0.13f), Color(4.1f, 2.3f, 3.1f));//Ezüst (n/k).....0.14/4.1, 0.16/2.3, 0.13/3.1

		objects[objectCount++] = new Cylinder(goldMaterial, brawnMaterial, Vector(0.0f, 0.0f, 1.5f), Vector(0.0f, 1.0f, 0.0f), 0.0f, 3.0f);
		objects[objectCount++] = new Cylinder(goldMaterial, brawnMaterial, Vector(-0.5f, 0.0f, 0.2f), Vector(0.0, 1.0f, 0.0f), 0.7f, 0.2f);
		objects[objectCount++] = new Cylinder(glassMaterial, brawnMaterial, Vector(0.5f, 0.0f, 0.2f), Vector(0.0f, 1.0f, 0.0f), 0.9f, 0.15f);
		objects[objectCount++] = new Cylinder(goldMaterial, goldMaterial, Vector(-0.3f, 0.339324f, 0.200015f), Vector(1.0f, 0.0f, 0.000073f), 0.3f, 0.05f);
		objects[objectCount++] = new Cylinder(goldMaterial, goldMaterial, Vector(-0.7f, 0.156781f, 0.199909f), Vector(-1.0f, 0.0f, 0.000456f), 0.3f, 0.05f);
        objects[objectCount++] = new Cylinder(goldMaterial, goldMaterial, Vector(-0.15f, 0.339324f, 0.200015f), Vector(0.0f, 1.0f, 0.0f), 0.15f, 0.03f);
		objects[objectCount++] = new Cylinder(goldMaterial, goldMaterial, Vector(-0.85f, 0.156781f, 0.199909f), Vector(0.0f, 1.0f, 0.0f), 0.25f, 0.03f);
		objects[objectCount++] = new Cylinder(glassMaterial, goldMaterial, Vector(0.35f, 0.440651f, 0.200176f), Vector(-1.0f, 0.0f, 0.001175f), 0.3f, 0.05f);
		objects[objectCount++] = new Cylinder(glassMaterial, goldMaterial, Vector(0.2f, 0.495651f, 0.200176f), Vector( 0.0f, 1.0f, 0.0f), 0.15f, 0.03f);

		objects[objectCount++] = new Paraboloid(silverMaterial, Vector(0.0f, 1.2f, -0.1f), Vector(0.0f, -1.0f, 0.0f), 1.2f);

		La_AmbientLight = Color(0.2f, 0.2f, 0.2f);
		SkyColor = Color(0.0f, 0.5f, 1.0f);

		lights[lightCount++] = Light(Vector(3.0f, 5.0f, 3.0f) * 1.5f, Color(0.3f, 0.0f, 0.0f) * 500.0f);
		lights[lightCount++] = Light(Vector(0.0f, 5.0f, 1.0f) * 1.5f, Color(0.0f, 0.3f, 0.0f) * 500.0f);
		lights[lightCount++] = Light(Vector(-3.0f, 5.0f, 3.0f) * 1.5f, Color(0.0f, 0.0f, 0.3f) * 500.0f);
	}

	void Render()
	{
		for (int i = 0; i < PhotonsToShoot; i++)
		{
			Vector ShootDirection;

			do
			{
				ShootDirection = Vector(RND, RND, RND);

			} while (ShootDirection.x * ShootDirection.x + ShootDirection.y * ShootDirection.y + ShootDirection.z * ShootDirection.z > 1.0f);

			ShootDirection.Normalize();
		}

		for (int x = 0; x < screenWidth; x++)
		{
			for (int y = 0; y < screenHeight; y++)
			{
				Ray actualRay = camera->GetRay(x, y);

				Color finalColor = RayTrace(actualRay);

				image[y*screenWidth * 3 + x * 3 + 0] = finalColor.r;
				image[y*screenWidth * 3 + x * 3 + 1] = finalColor.g;
				image[y*screenWidth * 3 + x * 3 + 2] = finalColor.b;
			}
		}

		ToneMapping();
	}


	Collide IntersectWorld(Ray ray)
	{
		Collide collide;
		for (int i = 0; i < objectCount; i++)
		{
			Collide newCollide = objects[i]->Intersect(ray);

			if (newCollide.t > 0.0f)
			{
				if (collide.t < 0.0f || newCollide.t < collide.t)
				{
					collide = newCollide;
				}
			}
		}

		if (collide.t > 0.0f)
		{
			collide.normalV.Normalize();
		}

		return collide;
	}

	void Shoot(Color power, Ray ray, int depth = 0)
	{
	    int x=-5, y=-5;
		if (depth > depth_Max)
		{
			return;
		}

		Collide collide = IntersectWorld(ray);

		if (collide.t < 0.0f)
		{
			return;
		}

		if (collide.material.IsReflective == true)
		{
			Ray reflectedRay;
			reflectedRay.rOrigo = collide.position;
			collide.material.DirOfReflection(reflectedRay.rDirection, collide.normalV, ray.rDirection);
			Color F = collide.material.CalculateFresnel(collide.normalV, ray.rDirection);
			F = F / 2.0f;

			power = power * F;

			Shoot(power, reflectedRay, depth + 1);
		}
		else
		{
			if (depth > 0 && collide.material.flat == true)
			{
			    while(x<6) {
                    while(y<6) {
                        int indexX = int(collide.position.x / photonConst * (MapSize / 2) + (MapSize / 2)) + x;
						int indexY = int(collide.position.z / photonConst * (MapSize / 2) + (MapSize / 2)) + y;

						if (x == 0 && y == 0)
						{
							PMap[indexX][indexY] = PMap[indexX][indexY] + power;
						}
						else
						{
							if (indexX > 0 && indexX < MapSize && indexY > 0 && indexY < MapSize)
							{
								Vector center = Vector(indexX, indexY);
								Vector tmp = Vector(indexX - x, indexY - y);
								if ((center - tmp).Length() < 5)
								{
									PMap[indexX][indexY] = PMap[indexX][indexY] + power / 2.0f;
								}
							}
						}
                        y++;
                    }
                    x++;
			    }
			}
		}
	}

	void ToneMapping()
	{
		float  LuminanceAll = 0.0f;
		for (int x = 0; x<screenWidth; x++)
		{
			for (int y = 0; y<screenHeight; y++)
			{
				LuminanceAll += 0.21f*image[y*screenWidth * 3 + x * 3 + 0] + 0.72f*image[y*screenWidth * 3 + x * 3 + 1] + 0.07f*image[y*screenWidth * 3 + x * 3 + 2];
			}
		}

		float AvgLuminance = LuminanceAll / (600.0f*600.0f);
		float Alpha = 0.65f;

		for (int x = 0; x<screenWidth; x++)
		{
			for (int y = 0; y<screenHeight; y++)
			{
				image[y*screenWidth * 3 + x * 3 + 0] *= Alpha / AvgLuminance;
				image[y*screenWidth * 3 + x * 3 + 1] *= Alpha / AvgLuminance;
				image[y*screenWidth * 3 + x * 3 + 2] *= Alpha / AvgLuminance;
			}
		}
	}
};

World world;

// Inicializacio, a program futasanak kezdeten, az OpenGL kontextus letrehozasa utan hivodik meg (ld. main() fv.)
void onInitialization() {
	world.Build();
	world.Render();
}

// Rajzolas, ha az alkalmazas ablak ervenytelenne valik, akkor ez a fuggveny hivodik meg
void onDisplay() {
	glClearColor(0.1f, 0.2f, 0.3f, 1.0f);		// torlesi szin beallitasa
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // kepernyo torles

	glDrawPixels(screenWidth, screenHeight, GL_RGB, GL_FLOAT, image);

	glutSwapBuffers();     				// Buffercsere: rajzolas vege

}

// Billentyuzet esemenyeket lekezelo fuggveny (lenyomas)
void onKeyboard(unsigned char key, int x, int y) {
	if (key == 'd') glutPostRedisplay(); 		// d beture rajzold ujra a kepet

}

// Billentyuzet esemenyeket lekezelo fuggveny (felengedes)
void onKeyboardUp(unsigned char key, int x, int y) {

}

// Eger esemenyeket lekezelo fuggveny
void onMouse(int button, int state, int x, int y) {
	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)   // A GLUT_LEFT_BUTTON / GLUT_RIGHT_BUTTON illetve GLUT_DOWN / GLUT_UP
		glutPostRedisplay(); 						 // Ilyenkor rajzold ujra a kepet
}

// Eger mozgast lekezelo fuggveny
void onMouseMotion(int x, int y)
{

}

// `Idle' esemenykezelo, jelzi, hogy az ido telik, az Idle esemenyek frekvenciajara csak a 0 a garantalt minimalis ertek
void onIdle() {
	long time = glutGet(GLUT_ELAPSED_TIME);		// program inditasa ota eltelt ido

}


// A C++ program belepesi pontja, a main fuggvenyt mar nem szabad bantani
int main(int argc, char **argv) {
	glutInit(&argc, argv); 				// GLUT inicializalasa
	glutInitWindowSize(600, 600);			// Alkalmazas ablak kezdeti merete 600x600 pixel
	glutInitWindowPosition(100, 100);			// Az elozo alkalmazas ablakhoz kepest hol tunik fel
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);	// 8 bites R,G,B,A + dupla buffer + melyseg buffer

	glutCreateWindow("Grafika hazi feladat");		// Alkalmazas ablak megszuletik es megjelenik a kepernyon

	glMatrixMode(GL_MODELVIEW);				// A MODELVIEW transzformaciot egysegmatrixra inicializaljuk
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);			// A PROJECTION transzformaciot egysegmatrixra inicializaljuk
	glLoadIdentity();

	onInitialization();					// Az altalad irt inicializalast lefuttatjuk

	glutDisplayFunc(onDisplay);				// Esemenykezelok regisztralasa
	glutMouseFunc(onMouse);
	glutIdleFunc(onIdle);
	glutKeyboardFunc(onKeyboard);
	glutKeyboardUpFunc(onKeyboardUp);
	glutMotionFunc(onMouseMotion);

	glutMainLoop();					// Esemenykezelo hurok

	return 0;
}

