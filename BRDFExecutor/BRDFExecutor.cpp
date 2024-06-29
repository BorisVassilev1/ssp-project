#include <glm/glm.hpp>

#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <Image.hpp>
#include <third_party/stb_image_write.h>

#include <Task.h>
#include <Executor.h>
#include <TaskSystem.h>

struct BRDFBuilder : TaskSystem::Executor {
	using uint = unsigned int;
	ImageData		 image;
	std::mutex		 initMutex;
	std::vector<int> perThreadProgress;
	std::atomic<int> completedThreads = 0;
	int				 size;
	std::string		 name;
	uint			 SAMPLE_COUNT;

	BRDFBuilder(std::unique_ptr<TaskSystem::Task> taskToExecute) : Executor(std::move(taskToExecute)) {
		size		 = task->GetIntParam("size").value();
		name		 = task->GetStringParam("name").value();
		SAMPLE_COUNT = task->GetIntParam("sample_count").value();
		image.init(size, size);
	}

	virtual ~BRDFBuilder() {}

	virtual ExecStatus ExecuteStep(int threadIndex, int threadCount) {
		if (perThreadProgress.empty()) {
			std::lock_guard<std::mutex> lock(initMutex);
			if (perThreadProgress.empty()) {
				perThreadProgress.resize(threadCount, 0);
				for (int c = 0; c < int(perThreadProgress.size()); c++) {
					perThreadProgress[c] = c;
				}
			}
		}

		int &idx = perThreadProgress[threadIndex];

		if (idx >= size * size) {
			if (completedThreads.load() == threadCount) {
				return ES_Stop;
			} else {
				return ES_Continue;
			}
		}

		const int r = idx / size;
		const int c = idx % size;

		glm::vec2 res		   = IntegrateBRDF((float)c / size, (float)r / size);
		image(c, size - r - 1) = Color(res.x, res.y, 0);

		idx += threadCount;

		if (idx >= size * size) {
			if (completedThreads.fetch_add(1) == threadCount - 1) {
				const std::string resultImage = name + ".png";
				const PNGImage	 &png		  = image.createPNGData();
				const int		  success = stbi_write_png(resultImage.c_str(), size, size, PNGImage::componentCount(),
														   png.data.data(), sizeof(PNGImage::Pixel) * size);
				assert(success == 1);
				return ES_Stop;
			}
		}
		return ES_Continue;
	};

   private:
	float RadicalInverse_VdC(uint bits) {
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return float(bits) * 2.3283064365386963e-10;	 // / 0x100000000
	}

	glm::vec2 Hammersley(uint i, uint N) { return glm::vec2(float(i) / float(N), RadicalInverse_VdC(i)); }

	glm::vec3 ImportanceSampleGGX(glm::vec2 Xi, glm::vec3 N, float roughness) {
		float a = roughness * roughness;

		float phi	   = 2.0 * PI * Xi.x;
		float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
		float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

		// from spherical coordinates to cartesian coordinates
		glm::vec3 H;
		H.x = cos(phi) * sinTheta;
		H.y = sin(phi) * sinTheta;
		H.z = cosTheta;

		// from tangent-space vector to world-space sample vector
		glm::vec3 up		= abs(N.z) < 0.999 ? glm::vec3(0.0, 0.0, 1.0) : glm::vec3(1.0, 0.0, 0.0);
		glm::vec3 tangent	= glm::normalize(glm::cross(up, N));
		glm::vec3 bitangent = glm::cross(N, tangent);

		glm::vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
		return glm::normalize(sampleVec);
	}

	float GeometrySchlickGGX(float NdotV, float roughness) {
		float a = roughness;
		float k = (a * a) / 2.0;

		float nom	= NdotV;
		float denom = NdotV * (1.0 - k) + k;

		return nom / denom;
	}

	float GeometrySmith(glm::vec3 N, glm::vec3 V, glm::vec3 L, float roughness) {
		float NdotV = glm::max(glm::dot(N, V), 0.0f);
		float NdotL = glm::max(glm::dot(N, L), 0.0f);
		float ggx2	= GeometrySchlickGGX(NdotV, roughness);
		float ggx1	= GeometrySchlickGGX(NdotL, roughness);

		return ggx1 * ggx2;
	}

	glm::vec2 IntegrateBRDF(float NdotV, float roughness) {
		glm::vec3 V;
		V.x = sqrt(1.0 - NdotV * NdotV);
		V.y = 0.0;
		V.z = NdotV;

		float A = 0.0;
		float B = 0.0;

		glm::vec3 N = glm::vec3(0.0, 0.0, 1.0);

		for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
			glm::vec2 Xi = Hammersley(i, SAMPLE_COUNT);
			glm::vec3 H	 = ImportanceSampleGGX(Xi, N, roughness);
			glm::vec3 L	 = glm::normalize(H * 2.0f * glm::dot(V, H) - V);

			float NdotL = glm::max(L.z, 0.0f);
			float NdotH = glm::max(H.z, 0.0f);
			float VdotH = glm::max(dot(V, H), 0.0f);

			if (NdotL > 0.0) {
				float G		= GeometrySmith(N, V, L, roughness);
				float G_Vis = (G * VdotH) / (NdotH * NdotV);
				float Fc	= glm::pow(1.0 - VdotH, 5.0);

				A += (1.0 - Fc) * G_Vis;
				B += Fc * G_Vis;
			}
		}
		A /= float(SAMPLE_COUNT);
		B /= float(SAMPLE_COUNT);
		return glm::vec2(A, B);
	}
};

TaskSystem::Executor *ExecutorConstructorImpl(std::unique_ptr<TaskSystem::Task> taskToExecute) {
	return new BRDFBuilder(std::move(taskToExecute));
}

IMPLEMENT_ON_INIT() { ts.Register("BRDFBuilder", &ExecutorConstructorImpl); }
