#include <iostream>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char *description) {
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace TaskSystem {
class Window {
	GLFWwindow *window;

public:
	Window(uint width, uint height, const std::string &name) {
		glfwSetErrorCallback(glfw_error_callback);
		if (!glfwInit()) return;
		window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
		if (window == nullptr) return;
		glfwMakeContextCurrent(window);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		(void)io;
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 130");

		ImFontConfig cfg;
		cfg.SizePixels = 100.f;
		ImFont *f	   = io.Fonts->AddFontDefault();
	}

	void SwapBuffers() {
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	void BeginFrame() {
		glfwPollEvents();
		if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			glfwSetWindowShouldClose(window, true);
		}

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	bool shouldClose() {
		return glfwWindowShouldClose(window);
	}
};
}	  // namespace TaskSystem
