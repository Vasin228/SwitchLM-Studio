
#!/usr/bin/env python3
"""colab_train_game_135m.py - Google Colab script for fine-tuning SmolLM2-135M
on Nintendo Switch game guides and exporting to Q3_K_M GGUF (~52 MB)."""

import os
import sys
import subprocess

def install_dependencies():
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q",
        "transformers", "peft", "datasets", "accelerate",
        "torch", "sentencepiece", "protobuf", "huggingface_hub"])
    if not os.path.exists("/content/llama.cpp"):
        subprocess.check_call(["git", "clone", "https://github.com/ggerganov/llama.cpp.git",
                               "/content/llama.cpp"])

def load_model_and_tokenizer(model_name="HuggingFaceTB/SmolLM2-135M"):
    from transformers import AutoModelForCausalLM, AutoTokenizer
    tokenizer = AutoTokenizer.from_pretrained(model_name)
    model = AutoModelForCausalLM.from_pretrained(model_name,
        torch_dtype="auto", device_map="auto")
    return model, tokenizer

def prepare_dataset(data_dir="/content/game_guides"):
    from datasets import Dataset
    texts = []
    for fname in os.listdir(data_dir):
        if fname.endswith(".txt"):
            with open(os.path.join(data_dir, fname), "r", encoding="utf-8") as f:
                texts.append({"text": f.read()})
    return Dataset.from_list(texts)

def train_and_export():
    print("=== SwitchLM: SmolLM2-135M Game Assist Training ===")
    print("Target: Q3_K_M GGUF export (~52 MB) for Nintendo Switch")
    install_dependencies()
    model_name = os.environ.get("BASE_MODEL", "HuggingFaceTB/SmolLM2-135M")
    print("Loading %s..." % model_name)
    model, tokenizer = load_model_and_tokenizer(model_name)
    data_dir = os.environ.get("DATA_DIR", "/content/game_guides")
    if os.path.exists(data_dir):
        print("Loading dataset from %s..." % data_dir)
        dataset = prepare_dataset(data_dir)
    else:
        print("Warning: %s not found, using dummy data" % data_dir)
        from datasets import Dataset
        dataset = Dataset.from_list([{"text": "Sample game guide text."}])
    from peft import LoraConfig, get_peft_model, TaskType
    lora_config = LoraConfig(
        r=16,
        lora_alpha=32,
        target_modules=["q_proj", "v_proj", "k_proj", "o_proj",
                        "gate_proj", "up_proj", "down_proj"],
        lora_dropout=0.05,
        bias="none",
        task_type=TaskType.CAUSAL_LM
    )
    model = get_peft_model(model, lora_config)
    model.print_trainable_parameters()
    from transformers import TrainingArguments, Trainer
    output_dir = os.environ.get("OUTPUT_DIR", "/content/switchlm-135m-lora")
    training_args = TrainingArguments(
        output_dir=output_dir,
        per_device_train_batch_size=4,
        gradient_accumulation_steps=4,
        warmup_steps=100,
        max_steps=1000,
        learning_rate=2e-4,
        fp16=True,
        logging_steps=10,
        save_steps=500,
        report_to="none",
    )
    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=dataset,
        tokenizer=tokenizer,
    )
    print("Starting LoRA SFT training...")
    trainer.train()
    adapter_dir = os.path.join(output_dir, "adapter")
    model.save_pretrained(adapter_dir)
    tokenizer.save_pretrained(adapter_dir)
    print("Adapter saved to %s" % adapter_dir)
    from peft import PeftModel
    base_model = AutoModelForCausalLM.from_pretrained(model_name,
        torch_dtype="auto", device_map="auto")
    merged = PeftModel.from_pretrained(base_model, adapter_dir)
    merged = merged.merge_and_unload()
    merged_dir = os.path.join(output_dir, "merged")
    merged.save_pretrained(merged_dir)
    tokenizer.save_pretrained(merged_dir)
    llama_dir = "/content/llama.cpp"
    convert_script = os.path.join(llama_dir, "convert_hf_to_gguf.py")
    gguf_out = os.path.join(output_dir, "SwitchLM-GameAssist-135M-f16.gguf")
    subprocess.check_call([sys.executable, convert_script, merged_dir,
                           "--outfile", gguf_out, "--outtype", "f16"])
    quantize_tool = os.path.join(llama_dir, "quantize")
    if not os.path.exists(quantize_tool):
        subprocess.check_call(["make", "quantize"], cwd=llama_dir)
    q3_out = os.path.join(output_dir, "SwitchLM-GameAssist-135M-Q3_K_M.gguf")
    subprocess.check_call([quantize_tool, gguf_out, q3_out, "Q3_K_M"])
    size_mb = os.path.getsize(q3_out) / (1024 * 1024)
    print("")
    print("=== Training Complete ===")
    print("Output: %s" % q3_out)
    print("Size: %.1f MB" % size_mb)
    print("Copy to: sdmc:/switch/SwitchLM/micro/SwitchLM-GameAssist-135M-Q3_K_M.gguf")

if __name__ == "__main__":
    train_and_export()

