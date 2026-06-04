import google.generativeai as genai
import os

# REPLACE THIS WITH YOUR ACTUAL KEY
GOOGLE_CHECK_API_KEY = os.environ.get('GOOGLE_CHECK_API_KEY')

genai.configure(api_key=GOOGLE_CHECK_API_KEY)

print("🔍 Checking available models for your key...")
try:
    for m in genai.list_models():
        if 'generateContent' in m.supported_generation_methods:
            print(f" - {m.name}")
except Exception as e:
    print(f"❌ Error: {e}")
