#pragma once
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace ControlComponent
{
	class Mouse {
	public:
		// parameters
		glm::vec2 currentPos;
		glm::vec2 previousPos;
		bool isActivated;

		// constructor
		Mouse();
	};

	class Camera {

	public:
		// enum
		enum SpeedChangeMode {SpeedUp, SpeedDown, NoChange};

		// parameter
		glm::vec3 camTranslation;
		glm::vec3 camRotation;
		float moveSpeed;
		SpeedChangeMode speedChangeMode;
		bool ifKeyQPressed, ifKeyWPressed, ifKeyEPressed, ifKeyAPressed, ifKeySPressed, ifKeyDPressed;
		double currentTime, previousTime;

		// functions
		void translate_camera();
		void rotate_camera(glm::vec2 screenOffset);
		void change_move_speed();
		glm::mat4 get_view_matrix();

		// constructor
		Camera();

	};
}


