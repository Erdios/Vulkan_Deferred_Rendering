#include "camera_control.h"
#include <GLFW/glfw3.h>
#include <iostream>

namespace ControlComponent
{
	// functions
	void Camera::translate_camera()
	{
		currentTime = glfwGetTime();

		double deltaTime = currentTime - previousTime;

		if (deltaTime < 0.01)
		{
			return;
		}


		glm::quat quaternion = glm::quat(camRotation);
		glm::mat4 rotatoinMatrix = glm::toMat4(quaternion);
		
		if(ifKeyWPressed)
			camTranslation -= glm::vec3(rotatoinMatrix[2][0], rotatoinMatrix[2][1], rotatoinMatrix[2][2]) * moveSpeed;
			
		if(ifKeySPressed)
			camTranslation += glm::vec3(rotatoinMatrix[2][0], rotatoinMatrix[2][1], rotatoinMatrix[2][2]) * moveSpeed;
			
		if(ifKeyAPressed)
			camTranslation -= glm::vec3(rotatoinMatrix[0][0], rotatoinMatrix[0][1], rotatoinMatrix[0][2]) * moveSpeed;
			
		if(ifKeyDPressed)
			camTranslation += glm::vec3(rotatoinMatrix[0][0], rotatoinMatrix[0][1], rotatoinMatrix[0][2]) * moveSpeed;
			
		if(ifKeyQPressed)
			camTranslation -= glm::vec3(0.f, 1.f, 0.f) * moveSpeed;
			
		if(ifKeyEPressed)
			camTranslation += glm::vec3(0.f, 1.f, 0.f) * moveSpeed;
		

		previousTime = currentTime;
	}

	void Camera::rotate_camera(glm::vec2 screenOffset)
	{
		camRotation -= glm::vec3(0.f, 0.005f, 0.f) * screenOffset.x * moveSpeed;
		camRotation -= glm::vec3(0.005f, 0.f, 0.f) * screenOffset.y * moveSpeed;

		if (camRotation[0] >= 1.55)
		{
			camRotation[0] = 1.55;
		}

		if (camRotation[0] <= -1.55)
		{
			camRotation[0] = -1.55;
		}
	}

	glm::mat4 Camera::get_view_matrix()
	{
		change_move_speed();
		translate_camera();

		glm::mat4 yawMatrix = glm::toMat4(glm::quat({ 0.f, -camRotation.y, 0.f }));
		glm::mat4 pitchMatrix = glm::toMat4(glm::quat({ -camRotation.x, 0.f , 0.f }));

		return pitchMatrix * yawMatrix * glm::translate(-camTranslation);
	}

	void Camera::change_move_speed()
	{

		switch (speedChangeMode)
		{
		case SpeedUp:
			if(moveSpeed<3.f) moveSpeed *= 1.1;
			break;
		case SpeedDown:
			if(moveSpeed>0.1f)moveSpeed *= 0.9;
			break;
		case NoChange:
			moveSpeed = 1.0f;
			break;
		}

	}
	// constructor
	Camera::Camera() : camTranslation(0.0f), camRotation(0.0f), moveSpeed(1.0f), speedChangeMode(Camera::NoChange),
		ifKeyQPressed(false), ifKeyWPressed(false), ifKeyEPressed(false), ifKeyAPressed(false), ifKeySPressed(false), 
		ifKeyDPressed(false),previousTime(0.f), currentTime(0.f)
	{
	}




	Mouse::Mouse():isActivated(false),currentPos(0.f),previousPos(0.f)
	{}

}