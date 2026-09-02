# Bölüm 6: Oyun Motoru Entegrasyonu: SDL, Vulkan, Hot-Reloading ve Shader Derleme

Modern 3D oyun motorlarında performans ve geliştirme hızı en kritik iki unsurdur. Geleneksel oyun motorlarında mantık kodları (gameplay logic) genellikle **Lua** veya **C# (Mono/IL2CPP)** gibi betik dilleriyle yazılır. Ancak bu diller sanal makine (Virtual Machine) katmanı veya yorumlayıcı (interpreter) yükü nedeniyle native C++ performansına ulaşamaz.

LLVM altyapısı, custom bir oyun programlama dili geliştirirken hem **native C++ hızında çalışan JIT (Just-In-Time)** betik çalışma zamanı oluşturmanıza hem de oyun motorunuzun **Vulkan** ve **SDL** katmanlarıyla tam entegre çalışmasına olanak tanır.

Bu bölümde, özel dilinizle yazılmış oyun betiklerinin SDL3/Vulkan mimarisine nasıl entegre edileceğini, C++ motor fonksiyonlarının JIT ortamına nasıl bağlanacağını (binding), oyun çalışırken koda anında müdahale etmeyi sağlayan **Canlı Kod Yenileme (Hot-Reloading)** mekanizmasını ve Vulkan için **LLVM üzerinden SPIR-V Shader** üretimi süreçlerini en ince ayrıntısına kadar inceleyeceğiz.

---

## 6.1 Oyun Motoru Mimarisi ve LLVM Betik Çalışma Zamanı

Bir oyun motoru temelde ana döngü (Game Loop), girdi yönetimi (SDL), grafik işleme (Vulkan), fizik simülasyonu ve betik sistemi (LLVM JIT) bileşenlerinden oluşur.

```
+-----------------------------------------------------------------------+
|                         OYUN ANA DÖNGÜSÜ (Game Loop)                  |
|                                                                       |
|  +-------------------+  +--------------------+  +------------------+  |
|  |   SDL3 Girdileri  |  |  Fizik Motoru      |  | LLVM JIT Betik   |  |
|  |   (Keyboard/Mouse)|  |  (Bullet/PhysX)    |  | (OnUpdate/Render)|  |
|  +---------+---------+  +---------+----------+  +--------+---------+  |
|            |                      |                      |            |
|            +----------------------+----------------------+            |
|                                   |                                   |
|                                   v                                   |
|                  +---------------------------------+                  |
|                  |     Vulkan Rendering Engine     |                  |
|                  | (VkCommandBuffer / SPIR-V)      |                  |
|                  +---------------------------------+                  |
+-----------------------------------------------------------------------+
```

LLVM JIT betik motorunun oyun döngüsü içerisindeki görevi, her karede (frame) çağrılan `OnUpdate(float deltaTime)` ve `OnRender()` gibi betik fonksiyonlarını **sıfır sanal makine overhead'i** ile doğrudan işlemci komut seti seviyesinde çalıştırmaktır.

---

## 6.2 C++ Motor API'sini LLVM JIT Ortamına Bağlama (Native Binding)

Oyun dilinizin C++ tarafında tanımlı olan matematiksel yapı tiplerine (`Vector3`, `Transform`) ve motor fonksiyonlarına (`LogMessage`, `SetEntityPosition`, `GetDeltaTime`) erişmesi gerekir.

LLVM ORC JIT mimarisinde, C++ fonksiyonlarının bellekteki adresleri `JITDylib` sembol tablosuna tanımlanarak betik kodlarının bu fonksiyonları doğrudan çağırması sağlanır.

### C++ Native Binding Entegrasyon Kodu (`src/engine_binding.cpp`)

```cpp
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <iostream>
#include <memory>

struct Vector3 {
    float x, y, z;
};

void Native_LogMessage(const char* message) {
    std::cout << "[GAME ENGINE SCRIPT LOG]: " << message << std::endl;
}

void Native_UpdateTransform(uint32_t entityID, Vector3* position) {
    std::cout << "[ENGINE] Entity ID: " << entityID
              << " Yeni Pozisyon -> X: " << position->x
              << " Y: " << position->y
              << " Z: " << position->z << std::endl;
}

int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto JITOrErr = llvm::orc::LLJITBuilder().create();
    if (!JITOrErr) {
        std::cerr << "LLJIT Oluşturulamadı!" << std::endl;
        return 1;
    }
    auto JIT = std::move(*JITOrErr);

    auto& MainDylib = JIT->getMainJITDylib();

    llvm::orc::MangleAndInterner Mangle(JIT->getExecutionSession(), JIT->getDataLayout());

    llvm::orc::SymbolMap Symbols;
    Symbols[Mangle("Native_LogMessage")] = {
        llvm::orc::ExecutorAddr::fromPtr(&Native_LogMessage),
        llvm::JITSymbolFlags::Exported
    };
    Symbols[Mangle("Native_UpdateTransform")] = {
        llvm::orc::ExecutorAddr::fromPtr(&Native_UpdateTransform),
        llvm::JITSymbolFlags::Exported
    };

    cantFail(MainDylib.define(llvm::orc::absoluteSymbols(Symbols)));

    auto Context = std::make_unique<llvm::LLVMContext>();
    auto Module = std::make_unique<llvm::Module>("GameScriptModule", *Context);
    llvm::IRBuilder<> Builder(*Context);

    llvm::Type* VoidTy = Builder.getVoidTy();
    llvm::Type* Int32Ty = Builder.getInt32Ty();
    llvm::Type* FloatTy = Builder.getFloatTy();
    llvm::Type* CharPtrTy = Builder.getInt8Ty()->getPointerTo();

    llvm::StructType* Vector3Ty = llvm::StructType::create(*Context, "Vector3");
    Vector3Ty->setBody({FloatTy, FloatTy, FloatTy});

    llvm::FunctionType* LogFuncTy = llvm::FunctionType::get(VoidTy, {CharPtrTy}, false);
    llvm::Function* LogFunc = llvm::Function::Create(LogFuncTy, llvm::Function::ExternalLinkage, "Native_LogMessage", Module.get());

    llvm::FunctionType* UpdateTransformTy = llvm::FunctionType::get(VoidTy, {Int32Ty, Vector3Ty->getPointerTo()}, false);
    llvm::Function* UpdateTransformFunc = llvm::Function::Create(UpdateTransformTy, llvm::Function::ExternalLinkage, "Native_UpdateTransform", Module.get());

    llvm::FunctionType* ScriptMainTy = llvm::FunctionType::get(VoidTy, {}, false);
    llvm::Function* ScriptMain = llvm::Function::Create(ScriptMainTy, llvm::Function::ExternalLinkage, "OnEngineStart", Module.get());

    llvm::BasicBlock* EntryBB = llvm::BasicBlock::Create(*Context, "entry", ScriptMain);
    Builder.SetInsertPoint(EntryBB);

    llvm::Value* LogStr = Builder.CreateGlobalStringPtr("LLVM Betik Sistemi Vulkan Motoruna Bağlandı!");
    Builder.CreateCall(LogFunc, {LogStr});

    llvm::AllocaInst* VecAlloc = Builder.CreateAlloca(Vector3Ty, nullptr, "playerPos");
    llvm::Value* XPtr = Builder.CreateStructGEP(Vector3Ty, VecAlloc, 0, "xPtr");
    llvm::Value* YPtr = Builder.CreateStructGEP(Vector3Ty, VecAlloc, 1, "yPtr");
    llvm::Value* ZPtr = Builder.CreateStructGEP(Vector3Ty, VecAlloc, 2, "zPtr");

    Builder.CreateStore(llvm::ConstantFP::get(FloatTy, 100.5f), XPtr);
    Builder.CreateStore(llvm::ConstantFP::get(FloatTy, 250.0f), YPtr);
    Builder.CreateStore(llvm::ConstantFP::get(FloatTy, -50.25f), ZPtr);

    llvm::Value* EntityID = Builder.getInt32(42);
    Builder.CreateCall(UpdateTransformFunc, {EntityID, VecAlloc});

    Builder.CreateRetVoid();

    cantFail(JIT->addIRModule(llvm::orc::ThreadSafeModule(std::move(Module), std::move(Context))));

    auto SymOrErr = JIT->lookup("OnEngineStart");
    if (!SymOrErr) {
        std::cerr << "OnEngineStart Sembolü Bulunamadı!" << std::endl;
        return 1;
    }

    using ScriptMainFn = void (*)();
    ScriptMainFn OnEngineStart = SymOrErr->toPtr<ScriptMainFn>();

    std::cout << "--- LLVM BETİK KODU ÇALIŞTIRILIYOR ---" << std::endl;
    OnEngineStart();
    std::cout << "--- BETİK ÇALIŞMASI TAMAMLANDI ---" << std::endl;

    return 0;
}
```

### Kodun Adım Adım Detaylı İncelemesi

1. **10-21. Satırlar:** Motor tarafında çağrılacak yerel C++ veri yapıları (`Vector3`) ve fonksiyonlar (`Native_LogMessage`, `Native_UpdateTransform`) tanımlanır.
2. **36-47. Satırlar:** `LLJIT` nesnesi oluşturulduktan sonra motorun `Native_LogMessage` ve `Native_UpdateTransform` C++ fonksiyon pointer'ları `ExecutorAddr::fromPtr()` ile LLVM ORC JIT sembol tablosuna (`absoluteSymbols`) eklenir. `MangleAndInterner` nesnesi hedef platformun sembol isimlendirme standartlarını (örneğin macOS/Windows alt tire ön eklerini) otomatik halleder.
3. **56-59. Satırlar:** LLVM IR seviyesinde C++ `Vector3` struct'ına karşılık gelen `{float, float, float}` gövdesine sahip bir `llvm::StructType` oluşturulur.
4. **61-65. Satırlar:** Dışarıdan bağlanan C++ fonksiyonlarının LLVM IR `llvm::Function` bildirimleri (`ExternalLinkage`) yapılır.
5. **75-84. Satırlar:** IRBuilder ile stack üzerinde bir `Vector3` alloca alanı tahsis edilir. `CreateStructGEP` (GetElementPtr) talimatı ile struct elemanlarının bellek adresleri hesaplanarak sırasıyla $100.5$, $250.0$ ve $-50.25$ değerleri belleğe yazılır.
6. **86-98. Satırlar:** `OnEngineStart` betik fonksiyonu JIT ile makine koduna dönüştürülür, adresi C++ fonksiyon pointer'ına cast edilerek çağrılır.

---

## 6.3 Canlı Kod Yenileme (Hot-Reloading) Mimarisi

Oyun geliştirme sürecinde motoru kapatıp açmadan, yazılan betik kodunun kaydedildiği anda oyuna aktarılması **Hot-Reloading** olarak adlandırılır.

LLVM ORC JIT mimarisinde bunu başarmak için dinamik `JITDylib` yönetimi kullanılır:

```
[Betik Dosyası Değişti (script.src)]
                |
                v
  [LLVM Lexer / Parser / AST]
                |
                v
   [LLVM IR Modülü Derlenir]
                |
                v
 [Eski JITDylib Silinir / Kaldırılır]
                |
                v
 [Yeni JITDylib Açılır ve Modül Yüklenir]
                |
                v
 [Motor Fonksiyon Pointer'ı Güncellenir] (Sıfır Kesinti)
```

### Hot-Reloading C++ Motor Sınıfı Kodu (`src/hot_reload_engine.cpp`)

```cpp
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <iostream>
#include <memory>
#include <string>

class ScriptHotReloader {
private:
    std::unique_ptr<llvm::orc::LLJIT> JIT;
    llvm::orc::JITDylib* CurrentScriptLib;
    int VersionCounter;

public:
    ScriptHotReloader() : VersionCounter(0), CurrentScriptLib(nullptr) {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        auto JITOrErr = llvm::orc::LLJITBuilder().create();
        if (!JITOrErr) {
            std::cerr << "JIT Motoru Başlatılamadı!" << std::endl;
            return;
        }
        JIT = std::move(*JITOrErr);
    }

    bool LoadOrReloadScript(float speedMultiplier) {
        VersionCounter++;
        std::string DylibName = "ScriptVersion_" + std::to_string(VersionCounter);

        if (CurrentScriptLib) {
            auto Err = JIT->getExecutionSession().removeJITDylib(*CurrentScriptLib);
            if (Err) {
                llvm::consumeError(std::move(Err));
            }
        }

        auto& NewDylib = JIT->getExecutionSession().createBareJITDylib(DylibName);
        NewDylib.addGenerator(
            cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
                JIT->getDataLayout().getGlobalPrefix()))
        );

        CurrentScriptLib = &NewDylib;

        auto Context = std::make_unique<llvm::LLVMContext>();
        auto Module = std::make_unique<llvm::Module>("GameScript_" + std::to_string(VersionCounter), *Context);
        llvm::IRBuilder<> Builder(*Context);

        llvm::FunctionType* FuncTy = llvm::FunctionType::get(Builder.getFloatTy(), {Builder.getFloatTy()}, false);
        llvm::Function* UpdateSpeedFunc = llvm::Function::Create(FuncTy, llvm::Function::ExternalLinkage, "CalculateMovementSpeed", Module.get());

        llvm::BasicBlock* Entry = llvm::BasicBlock::Create(*Context, "entry", UpdateSpeedFunc);
        Builder.SetInsertPoint(Entry);

        llvm::Value* BaseSpeed = llvm::ConstantFP::get(Builder.getFloatTy(), 10.0f);
        llvm::Value* Multiplier = UpdateSpeedFunc::arg_begin();
        llvm::Value* VersionBonus = llvm::ConstantFP::get(Builder.getFloatTy(), (float)VersionCounter * 5.0f);

        llvm::Value* TempSpeed = Builder.CreateFMul(BaseSpeed, Multiplier, "tempSpeed");
        llvm::Value* FinalSpeed = Builder.CreateFAdd(TempSpeed, VersionBonus, "finalSpeed");

        Builder.CreateRet(FinalSpeed);

        cantFail(JIT->addIRModule(*CurrentScriptLib, llvm::orc::ThreadSafeModule(std::move(Module), std::move(Context))));

        return true;
    }

    float ExecuteScript(float deltaTime) {
        auto SymOrErr = JIT->lookup(*CurrentScriptLib, "CalculateMovementSpeed");
        if (!SymOrErr) {
            std::cerr << "Fonksiyon bulunamadı!" << std::endl;
            return 0.0f;
        }

        using SpeedFn = float (*)(float);
        SpeedFn CalculateSpeed = SymOrErr->toPtr<SpeedFn>();
        return CalculateSpeed(deltaTime);
    }
};

int main() {
    ScriptHotReloader Reloader;

    std::cout << "--- KARE 1: Ilk Betik Yukleniyor ---" << std::endl;
    Reloader.LoadOrReloadScript(1.0f);
    std::cout << "Oyuncu Hizi (Kare 1): " << Reloader.ExecuteScript(1.5f) << std::endl;

    std::cout << "\n--- KARE 100: Oyun Esnasinda Betik Degisti (Hot-Reloading) ---" << std::endl;
    Reloader.LoadOrReloadScript(2.0f);
    std::cout << "Oyuncu Hizi (Kare 100 - Reload Sonrasi): " << Reloader.ExecuteScript(1.5f) << std::endl;

    return 0;
}
```

### Kodun Adım Adım Detaylı İncelemesi

1. **28-36. Satırlar:** Hot-reloading gerçekleştiğinde eski betik modülünü barındıran `JITDylib` belleği `removeJITDylib` ile ExecutionSession'dan güvenli bir şekilde silinir. Bu işlem bellek sızıntısını ve eski sembollerin yeni sürümlerle çakışmasını engeller.
2. **38-44. Satırlar:** `createBareJITDylib` ile yeni versiyona özel bağımsız bir `JITDylib` (örneğin `ScriptVersion_2`) oluşturulur.
3. **58-62. Satırlar:** Yeni derlenen LLVM IR içerisinde hareket hızı formülü $FinalSpeed = (BaseSpeed \times Multiplier) + VersionBonus$ şeklinde oluşturulur.
4. **82-94. Satırlar:** Oyun motoru çalışırken canlı derleme tetiklendiğinde `CalculateMovementSpeed` fonksiyon pointer'ı dinamik olarak yeni JIT bellek adresine yönlendirilir ve oyun duraksamadan güncel mantıkla çalışmaya devam eder.

---

## 6.4 Vulkan ve SDL3 Entegrasyonunda Özel Shader Derleme Hattı (SPIR-V)

Oyun motorlarında grafik işleme hattı (Vulkan) **SPIR-V** adı verilen ikili (binary) ara temsil biçimini kullanır. LLVM altyapısı, geliştirdiğiniz dilin grafik kodlarını veya HLSL/GLSL benzeri shader yapılarını doğrundan SPIR-V bytecode'una derlemenize imkan tanır.

```
[Custom Oyun Dili Shader Kodu]
              |
              v
     [LLVM IR Derlemesi]
              |
              v
 [LLVM SPIR-V Target Machine]
              |
              v
      [SPIR-V Bytecode]
              |
              v
[Vulkan VkShaderModule Oluşturma] (vkCreateShaderModule)
```

### LLVM IR'dan Vulkan SPIR-V ve VkShaderModule Üretim Kodu (`src/vulkan_shader_compiler.cpp`)

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>
#include <memory>

void GenerateVulkanVertexShader() {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();

    auto Context = std::make_unique<llvm::LLVMContext>();
    auto Module = std::make_unique<llvm::Module>("VulkanVertexShader", *Context);

    Module->setTargetTriple("spirv64-unknown-unknown");

    llvm::IRBuilder<> Builder(*Context);

    llvm::Type* VoidTy = Builder.getVoidTy();
    llvm::Type* FloatTy = Builder.getFloatTy();
    llvm::Type* Vec4Ty = llvm::FixedVectorType::get(FloatTy, 4);

    llvm::FunctionType* ShaderMainTy = llvm::FunctionType::get(VoidTy, {}, false);
    llvm::Function* ShaderMain = llvm::Function::Create(ShaderMainTy, llvm::Function::ExternalLinkage, "main", Module.get());

    llvm::BasicBlock* Entry = llvm::BasicBlock::Create(*Context, "entry", ShaderMain);
    Builder.SetInsertPoint(Entry);

    llvm::Value* PosVec = llvm::ConstantVector::get({
        llvm::ConstantFP::get(FloatTy, 0.0f),
        llvm::ConstantFP::get(FloatTy, 0.5f),
        llvm::ConstantFP::get(FloatTy, 0.0f),
        llvm::ConstantFP::get(FloatTy, 1.0f)
    });

    Builder.CreateRetVoid();

    std::string Error;
    const llvm::Target* Target = llvm::TargetRegistry::lookupTarget("spirv64", Error);
    if (!Target) {
        std::cerr << "SPIR-V Target Bulunamadı: " << Error << std::endl;
        return;
    }

    llvm::TargetOptions Options;
    auto TargetMachine = std::unique_ptr<llvm::TargetMachine>(
        Target->createTargetMachine("spirv64", "generic", "", Options, llvm::Reloc::PIC_)
    );

    Module->setDataLayout(TargetMachine->createDataLayout());

    llvm::SmallVector<char, 0> SPIRVBuffer;
    llvm::raw_svector_ostream OS(SPIRVBuffer);

    llvm::legacy::PassManager PM;
    if (TargetMachine->addPassesToEmitFile(PM, OS, nullptr, llvm::CodeGenFileType::CGFT_ObjectFile)) {
        std::cerr << "TargetMachine SPIR-V dosya üretimi sağlayamıyor!" << std::endl;
        return;
    }

    PM.run(*Module);

    std::cout << "--- VULKAN SPIR-V SHADER KODU BAŞARIYLA ÜRETİLDİ ---" << std::endl;
    std::cout << "Üretilen SPIR-V Bytecode Boyutu: " << SPIRVBuffer.size() << " Bayt" << std::endl;
    std::cout << "Bu bytecode 'vkCreateShaderModule' API'sine doğrudan verilebilir." << std::endl;
}

int main() {
    GenerateVulkanVertexShader();
    return 0;
}
```

### Kodun Adım Adım Detaylı İncelemesi

1. **20. Satır:** `Module->setTargetTriple("spirv64-unknown-unknown")` çağrısı ile LLVM modülünün hedef Mimarisi Khronos SPIR-V (Vulkan Shader Standardı) olarak belirlenir.
2. **26. Satır:** `llvm::FixedVectorType::get(FloatTy, 4)` kullanılarak GLSL/HLSL dillerindeki `vec4` / `float4` shader pozisyon verisi temsil edilir.
3. **40-45. Satırlar:** `TargetRegistry::lookupTarget("spirv64", Error)` vasıtasıyla LLVM'in SPIR-V arka yüzü (backend) sorgulanır.
4. **55-61. Satırlar:** `CodeGenFileType::CGFT_ObjectFile` modunda `PassManager` çalıştırılarak bellek içi ikili (binary) Vulkan SPIR-V modülü üretilir. Üretilen bu bayt dizisi doğrudan Vulkan `VkShaderModuleCreateInfo::pCode` alanına paslanarak ekran kartında çalıştırılmaya hazır hale getirilir.

---

## 6.5 Profil Tabanlı Optimizasyon (PGO) ve Performans Profilleme

Oyunlarda kare hızı düşüşlerini (frame drop) önlemek için en çok çalışan betik kodlarının (**Hot Path**) tespit edilip çalışma zamanında otomatik olarak tekrar optimize edilmesi gerekir.

```
[Betik Ilk Olarak JIT Ile Derlenir (Unoptimized / O0)]
                          |
                          v
    [Çalışma Zamanında Profil Verisi Toplanır (PGO)]
                          |
                          v
       [Döngü Sayısı > 10,000 Kez Çalıştı Mı?]
                     /         \
                 Evet           Hayır
                 /               \
                v                 v
   [LLVM O3 Pipeline +]      [Mevcut Kodla]
   [Inlining Tekrar JIT]     [Devam Et]
```

### LLVM Execution Count Instrumentasyonu

LLVM JIT motorunuza sayaç blokları ekleyerek bir betik fonksiyonunun kaç defa çalıştırıldığını takip edebilirsiniz:

1. **Instrumentasyon Pass'i:** Her `llvm::Function` girişine ve döngü başlarına bir global sayaç artırma (`atomicadd`) IR talimatı yerleştirilir.
2. **Dinamik Yeniden Derleme (Re-JIT):** Sayaç belirli bir eşik değeri (örneğin $10.000$ çağrı) aştığında, motor arka planda bir iş parçacığında (worker thread) ilgili fonksiyonu `PassBuilder::OptimizationLevel::O3` seviyesinde inlining ve loop unrolling uygulayarak yeniden derler.
3. **Sanal Tablo Güncellemesi:** Yeni yüksek performanslı fonksiyon adresi JIT sembol tablosunda eskisiyle değiştirilir.

Bu mimari sayesinde oyun motorunuz, tıpkı modern JavaScript V8 veya Java HotSpot JIT motorları gibi, oyun oynandıkça daha da hızlanan akıllı bir betik çalışma zamanına kavuşur.

---

## 6.6 Bölüm Özeti ve Son Söz

Bu kitap boyunca:
- **Bölüm 1'de:** LLVM mimarisini, C++ ortam kurulumunu ve ilk IR generation adımlarını,
- **Bölüm 2'de:** Frontend tasarımını, AST yapılarını, SSA formunu, PHI düğümlerini ve GEP bellek modelini,
- **Bölüm 3'te:** Middle-end optimizasyonlarını, New Pass Manager'ı ve kendi optimizasyon pass'inizi yazmayı,
- **Bölüm 4'te:** Backend kod üretimini, TableGen mimarisini, Instruction Selection ve MIR süreçlerini,
- **Bölüm 5'te:** ORC JIT, MLIR, SPIR-V, Clang Tooling, LLD ve Sanitizer araçlarını,
- **Bölüm 6'da:** Özel oyun dilinizi **SDL**, **Vulkan**, **Hot-Reloading** ve **PGO** ile eksiksiz bir oyun motoru betik sistemine dönüştürmeyi öğrendiniz.

Artık LLVM'i C++ ile ustalıkla kullanabilecek, kendi hayalinizdeki yüksek performanslı oyun programlama dilini ve derleyicisini inşa edebilecek tüm teorik ve pratik donanıma sahipsiniz!
