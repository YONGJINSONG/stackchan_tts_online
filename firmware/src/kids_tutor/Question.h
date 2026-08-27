#pragma once
#include <Arduino.h>
#include <vector>

struct Question {
  String id;
  String category;
  String mode;
  String domain;
  uint8_t level = 1;
  String levelCode;
  uint8_t difficulty = 1;
  String question;
  String tts;
  String answerType;
  String answer;
  std::vector<String> choices;
  String hint1;
  String hint2;
  String explanation;
  std::vector<String> speechAnswers;
  String answerLanguage;
  String image;

  // Spatial questions are rendered directly with M5GFX on CoreS3.
  String visualType;
  String visualData;
  std::vector<String> visualChoices;
};
