#include "imgui.h"
#include "imgui-SFML.h"

#include "fl/Headers.h"

#include "SFML/Graphics.hpp"
#include "SFML/System.hpp"

#include "FastNoise.h"

#include <memory>

const double PI = 3.14159265358979323846264338327950288419716939937510582;

using pEngine = std::unique_ptr<fl::Engine>;

template<class T>
union vec2
{
  sf::Vector2<T> vec;
  T arr[2];
  vec2()
    : vec()
  {}
  vec2(T x, T y)
    : vec(x, y)
  {}
};

// Simple distance forumula function, ignores y-distance
fl::scalar calcDistanceFromLine(const vec2<float> &linePos, const vec2<float> &carPos)
{
  vec2<float> diff(linePos.vec.x - carPos.vec.x, 0.f/*linePos.vec.y - carPos.vec.y*/);
  return static_cast<fl::scalar>(sqrtf((diff.vec.x * diff.vec.x) + (diff.vec.y * diff.vec.y)));
}

int main()
{
  // Setup FIS
  fl::FisImporter importer;
  fl::InputVariable *distanceInput, *velocityInput;
  pEngine engine1 = pEngine(importer.fromFile("frl_mamdani1.fis"));
  pEngine engine2 = pEngine(importer.fromFile("frl_mamdani2.fis"));
  pEngine engine3 = pEngine(importer.fromFile("frl_sugeno1.fis"));
  pEngine engine4 = pEngine(importer.fromFile("frl_sugeno2.fis"));
  pEngine engine5 = pEngine(importer.fromFile("old_fuzzyracingline.fis"));
  fl::Engine *engine = engine1.get();
  std::string status;
  bool engineOK = engine->isReady(&status);  
  if (engineOK)
  {
    distanceInput = engine->getInputVariable("distance");
    velocityInput = engine->getInputVariable("velocity");
  }

  sf::Vector2<unsigned int> windowSize(800, 600);

  sf::RenderWindow window(sf::VideoMode(windowSize.x, windowSize.y), "Fuzzy Logic - Racing Line", sf::Style::Titlebar | sf::Style::Close);
  window.setVerticalSyncEnabled(true);
  ImGui::SFML::Init(window);
  
  sf::Clock runtimeClock;
  sf::Clock deltaClock;
  sf::Color bgColour(100, 100, 100);

  //bool setLinePosToSinCurve = false;
  int selectedFIS = 0;
  bool manualModalOpen = false;

  // Setup FastNoise class
  int selectedLineController = 0;
  FastNoise noise(42);
  noise.SetInterp(FastNoise::Interp::Quintic);
  noise.SetFrequency(0.2);
  noise.SetFractalGain(0.4);
  noise.SetFractalLacunarity(1.5);
  noise.SetFractalOctaves(3);
  
  sf::RectangleShape line;
  vec2<float> linePos(400, 300);
  sf::Vector2f lineSize(10.f, 100.f);
  line.setSize(lineSize);
  line.setOrigin(sf::Vector2f(lineSize.x / 2, lineSize.y / 2));
  line.setPosition(linePos.vec);
  line.setFillColor(sf::Color::White);

  sf::RectangleShape car;
  vec2<float> carPos(200, 300);
  vec2<float> carMaxTurnVector(0.75f, 0.2f); // We'll multiply this with the value returned by the FIS, then calculate the angle
  vec2<float> dirVector(0.f, 0.f);
  float carSpeed = 250.f;
  sf::Vector2f carSize(30.f, 65.f);
  car.setSize(carSize);
  car.setOrigin(sf::Vector2f(carSize.x / 2, 12.5f)); // Set the origin where front wheels would be
  car.setPosition(carPos.vec);
  car.setFillColor(sf::Color::Red);
  fl::scalar lastdistance = linePos.vec.x - carPos.vec.x; // Cache the previous distance from the line to use to calculate the velocity relative to the line

  runtimeClock.restart();
  while (window.isOpen())
  {
    // Deal with window events
    sf::Event event;
    while (window.pollEvent(event))
    {
      ImGui::SFML::ProcessEvent(event);
      
      switch (event.type)
      {
      case sf::Event::Closed: window.close(); break;
       
      default:
        break;
      }
    }

    // ImGui update
    float delta = deltaClock.getElapsedTime().asSeconds();
    ImGui::SFML::Update(window, deltaClock.restart());

    ImGui::Begin("Debug Controls"); // begin window

    if (ImGui::BeginMainMenuBar())
    {
      if (ImGui::BeginMenu("Tools"))
      {
        ImGui::MenuItem("Manual FIS Input", NULL, &manualModalOpen);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    if (manualModalOpen)
    {
      ImGui::OpenPopup("ManualFIS");
      if (ImGui::BeginPopupModal("ManualFIS"))
      {
        static float distance = 0;
        static float velocity = 0;
        ImGui::Text("Drag the inputs below to change the inputs to the Fuzzy Inference System");
        ImGui::DragFloat("Distance From Racing Line", &distance, 0.001f, -1.f, 1.f);
        ImGui::DragFloat("Velocity Relative To Racing Line", &velocity, 0.001f, -1.f, 1.f);
        ImGui::Spacing();
        engine->setInputValue("distance", distance);
        engine->setInputValue("velocity", velocity);
        // Get FIS turn value
        engine->process();
        fl::scalar direction = engine->getOutputValue("direction");
        ImGui::Text("Direction: %.5f", direction);

        if (ImGui::Button("Close"))
        {
          ImGui::CloseCurrentPopup();
          manualModalOpen = false;
        }

        ImGui::EndPopup();
      }
    }

    ImGui::Text("Delta: %.3f FPS: %.2f", delta, 1/delta);

    // In case something goes wrong with the engine during import
    if (!engineOK)
    {
      if(status.length()) ImGui::Text(status.c_str());
      else(ImGui::Text("Status String Empty"));
    }

    // FIS Selector
    if (ImGui::Combo("Select Fuzzy Inference System", &selectedFIS, "Mamdani 1\0Mamdani 2\0Sugeno 1\0Sugeno 2\0Last Year's Fuzzy System\0"))
    {
      switch (selectedFIS)
      {
      case 0: engine = engine1.get(); break;
      case 1: engine = engine2.get(); break;
      case 2: engine = engine3.get(); break;
      case 3: engine = engine4.get(); break;
      case 4: engine = engine5.get(); break;
      default:
        break;
      }
    }

    if (ImGui::TreeNode("FIS Summary"))
    {
      switch (selectedFIS)
      {
      case 0:
        ImGui::TextWrapped("Mamdani 1 (file: frl_mamdani1.fis) is a basic mamdani fuzzy inference system. Membership functions are either defined as trapeziums or triangles. The mfs do not extend beyond the range of [-1, 1]. Uses Centroid defuzzification.");
        break;
      case 1:
        ImGui::TextWrapped("Mamdani 2 (file: frl_mamdani2.fis) is a slightly more complicated fuzzy inference system. It uses a mixture of 'pimf' or pi-shaped membership functions, along with triangle mfs. These membership functions allow for a smooth transition output values.");
        break;
      case 2:
        ImGui::TextWrapped("Sugeno 1 (file: frl_sugeno1.fis) is a sugeno fuzzy inference system, using what MATLAB calls 'constant' values for its outputs. It also uses 'pimf' or pi-shaped membership functions along with triangle mfs for its input variables.");
        break;
      case 3:
        ImGui::TextWrapped("Sugeno 2 (file: frl_sugeno2.fis) is a sugeno fuzzy inference system, using what MATLAB calls 'linear' values for its outputs. It also uses 'pimf' or pi-shaped membership functions along with triangle mfs for its input variables.");
        break;
      case 4:
        ImGui::TextWrapped("The 'fuzzy' inference system I submitted last year, very obviously not up to snuff but here for posterity and laughs.");
        break;
      default:
        break;
      }
      ImGui::TreePop();
    }

    ImGui::Combo("Line Position Controller", &selectedLineController, "Sine Curve\0Procedural Noise\0Manual\0");
    switch (selectedLineController)
    {
    case 0: // Sine Curve
    {
      linePos.vec.x = (windowSize.x / 2) - std::sinf(runtimeClock.getElapsedTime().asSeconds()) * (windowSize.x / 4);
      break;
    }
    case 1: // Noise
    {
      linePos.vec.x = (windowSize.x / 2) - noise.GetSimplexFractal(42.f + runtimeClock.getElapsedTime().asSeconds(), -42.f + runtimeClock.getElapsedTime().asSeconds()) * (windowSize.x / 4);
      break;
    }
    case 2: // Manual
    {
      ImGui::DragFloat("Line Position", &(linePos.arr[0]), 1.f, windowSize.x / 2 - windowSize.x / 4, windowSize.x/2 + windowSize.x / 4);
      break;
    }
    default:
      break;
    }
    
    line.setPosition(linePos.vec);   

    ImGui::DragFloat("Car Speed", &carSpeed, 0.1f, 100.f, 500.f);
    ImGui::DragFloat2("Max Card Turn Vector", carMaxTurnVector.arr, 0.001f, -0.75f, 0.75f, "%.3f");

    // Car update
    // Set FIS inputs   
    fl::scalar distance = (linePos.vec.x - carPos.vec.x);
    fl::scalar distanceNormalised = distance / (windowSize.x/4);
    if (distanceNormalised > 1.0) distanceNormalised = 1.0;
    if (distanceNormalised < -1.0) distanceNormalised = -1.0;

    fl::scalar diffDistance = (lastdistance - distance);
    fl::scalar velocityNormalised = (diffDistance / (windowSize.x/12)); // take the difference between the last distance and the new distance and divide it by the maximum distance on the screen
    if (velocityNormalised > 1.0) velocityNormalised = 1.0;
    if (velocityNormalised < -1.0) velocityNormalised = -1.0;

    engine->setInputValue("distance", distanceNormalised);
    engine->setInputValue("velocity", velocityNormalised);
    // Get FIS turn value
    engine->process();
    fl::scalar direction = engine->getOutputValue("direction");
    if (direction != direction) abort(); // NaN check

    // Convert direction into an angle to rotate the car by
    vec2<float> directionVector(direction * carMaxTurnVector.vec.x, carMaxTurnVector.vec.y);      
     
    // Apply new turn vector to current turn vector, cap if we reach max turn
    dirVector.vec.x = directionVector.vec.x;
    dirVector.vec.y = directionVector.vec.y;
    if (dirVector.vec.x > carMaxTurnVector.vec.x) dirVector.vec.x = carMaxTurnVector.vec.x;
    if (dirVector.vec.x < -carMaxTurnVector.vec.x) dirVector.vec.x = -carMaxTurnVector.vec.x;
    if (dirVector.vec.y > carMaxTurnVector.vec.y) dirVector.vec.y = carMaxTurnVector.vec.y;
    if (dirVector.vec.y < -carMaxTurnVector.vec.y) dirVector.vec.y = -carMaxTurnVector.vec.y;

    // Calculate the angle between the new vector and the vector (0, 1 (up)
    float dot = (0 * dirVector.vec.x) + (1 * dirVector.vec.y); // 0 * x + 1 * y = y
    float det = (0 * dirVector.vec.y) - (1 * dirVector.vec.x);
    float angleRads = std::atan2(det, dot);
    float angleDeg = (angleRads / PI) * 180;

    // Calculate the car's new position
    vec2<float> newPos( carPos.vec.x - (dirVector.vec.x * (carSpeed * delta))
                      , carPos.vec.y); // Y is fixed for this program, but would follow the same principle and x


    // Update the car's position
    carPos.vec.x = newPos.vec.x;
    car.setPosition(carPos.vec);
    
    // Set the car's rotation
    car.setRotation(angleDeg);

    // Update the old distance value
    lastdistance = distance;

    // Display data in window
    ImGui::Text("Distance from line: %.5f, normalised: %.5f", distance, distanceNormalised);
    ImGui::Text("Car velocity relative to line: %.5f, normalised: %.5f", diffDistance, velocityNormalised);
    ImGui::Text("Direction calculated by FIS: %.5f", direction);
    ImGui::Text("Calculated Direction Vector: (%.5f, %.5f)", directionVector.vec.x, directionVector.vec.y);
    ImGui::Text("DotProduct: %.5f Determinant: %.5f", dot, det);
    ImGui::Text("%.5f/%.5f: %.5f", dot, det, dot/det);
    ImGui::Text("Angle to rotate car by: (Radians) %.5f, (Degrees) %.5f", angleRads, angleDeg);
    ImGui::Spacing();
    ImGui::Text("Car Position: (%.5f, %.5f)", carPos.vec.x, carPos.vec.y);
    ImGui::Text("Car Direction Vector: (%.5f, %.5f)", dirVector.vec.x, dirVector.vec.y);

    ImGui::End(); // end window
    //ImGui::ShowDemoWindow();

    // Screen update
    window.clear(bgColour); // fill background with color
    
    window.draw(line);
    window.draw(car);
    ImGui::SFML::Render(window);
    window.display();
  }

  ImGui::SFML::Shutdown();

  return 0;
}
