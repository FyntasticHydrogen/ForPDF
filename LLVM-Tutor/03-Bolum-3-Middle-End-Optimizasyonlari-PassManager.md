# Bölüm 3: Middle-end Optimizasyonları ve New Pass Manager (NPM)

## 3.1 LLVM Optimizasyon Mimarisi ve Pass Kavramı

LLVM derleyici altyapısının en güçlü yönlerinden biri modüler **Pass (Geçiş)** mimarisidir. Bir **Pass**, LLVM IR üzerinde belirli bir analizi (Analysis Pass) yürüten veya IR'ı değiştiren/optimize eden (Transformation Pass) bağımsız bir C++ sınıfıdır.

Middle-end optimizasyon katmanı, kaynak dilden ve hedef donanımdan tamamen bağımsızdır. Amacı, ön yüzün ürettiği LLVM IR'ı matematiksel ve mantıksal olarak en sade, en hızlı ve en az bellek tüketen şekle getirmektir.

```
                      +-----------------------------+
                      |    Ham LLVM IR (Frontend)   |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   Mem2Reg Pass (alloca->SSA)|
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      | Constant Folding & InstCombine |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   CSE & Dead Code Elimination|
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |    Loop Unrolling Pass      |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |  Optimize Edilmiş LLVM IR   |
                      +-----------------------------+
```

---

## 3.2 New Pass Manager (NPM) vs. Legacy Pass Manager

LLVM 13 ve sonrasında eski (Legacy) Pass Manager tamamen kaldırılmış ve yerini **New Pass Manager (NPM)** almıştır. New Pass Manager'ın getirdiği temel yenilikler ve avantajlar şunlardır:

1. **Tür Güvenliği ve Derleme Zamanı Şablonları (Templates & CRTP):** Kalıtım (vtable) yerine C++ şablonları kullanılarak fonksiyon çağrı maliyetleri düşürülmüştür.
2. **Esnek Analiz Önbellekleme (Analysis Management):** Bir optimizasyon passi IR'ı değiştirmediyse, önceki analiz sonuçları (örn: Dominator Tree) tekrar hesaplanmaz.
3. **Piyasa Standartları Pipeline Desteği:** `O1`, `O2`, `O3`, `Os`, `Oz` gibi hazır optimizasyon seviyeleri kolayca oluşturulabilir.

<div class="callout callout-info">
<div class="callout-title">NPM vs Legacy Performans Farkı</div>
New Pass Manager, şablon tabanlı yapısı ve akıllı analiz önbellekleme (Analysis Caching) mekanizması sayesinde büyük projelerde derleme sürelerini %10 ile %25 arasında kısaltmaktadır.
</div>

---

## 3.3 Temel Optimizasyon Teknikleri ve IR Üzerinde İncelenmesi

Oyun motorlarında en yüksek kare hızlarına (FPS) ulaşmak için kritik öneme sahip 4 ana optimizasyon tekniğini inceleyelim:

### 1. Sabit Katlama (Constant Folding)
Derleme zamanında değeri bilinen sabit ifadelerin hesaplanarak tek bir değere indirgenmesidir.

* **Optimize Edilmemiş IR:**
```llvm
%a = fadd float 10.0, 20.0
%b = fmul float %a, 2.0
```
* **Constant Folding Sonrası IR:**
```llvm
; Derleyici (10.0 + 20.0) * 2.0 = 60.0 işlemini derleme zamanında yapar!
%b = store float 60.0
```

### 2. Ortak Alt İfade Eleme (Common Subexpression Elimination - CSE)
Program içinde birden fazla kez tekrarlanan aynı matematiksel hesaplamaları tespit edip tek bir hesaplama sonucunu tekrar kullanmaktır. Oyun içi vektör uzaklık hesaplarında hayati önem taşır.

* **Ham IR:**
```llvm
%dx1 = fsub float %x2, %x1
%dy1 = fsub float %y2, %y1
; Tekrar eden aynı ifade:
%dx2 = fsub float %x2, %x1
```
* **CSE Sonrası IR:**
```llvm
%dx1 = fsub float %x2, %x1
%dy1 = fsub float %y2, %y1
; %dx2 ifadesi silindi, %dx1 yazmacı doğrudan kullanıldı
```

### 3. Ölü Kod Eleme (Dead Code Elimination - DCE)
Programın çıktısını veya durumunu etkilemeyen, sonucu hiçbir yerde kullanılmayan (unreachable / unused) talimatların silinmesidir.

* **Ham IR:**
```llvm
%unused_calc = fmul float %enemy_hp, 0.0 ; Sonucu hiçbir yerde kullanılmıyor
ret void
```
* **DCE Sonrası IR:**
```llvm
ret void ; %unused_calc tamamen temizlendi!
```

### 4. Döngü Açma (Loop Unrolling)
Döngü dallanma maliyetini (branch prediction ve sayaç artırma) azaltmak için döngü gövdesini ardışık olarak genişletmektir. Oyun motorlarında matris çarpmalarında ve SIMD vektörizasyonunda sıklıkla uygulanır.

---

## 3.4 Özel Bir LLVM Pass Yazımı (Modern C++ NPM)

Şimdi oyun derleyicimiz için custom bir LLVM Pass yazalım. Bu Pass, oyun kodumuzdaki tüm yavaş bölme işlemlerini (`fdiv`) çarpmaya (`fmul`) dönüştüren bir optimizasyon yapsın.

### C++ Kodu (`src/custom_pass.cpp`)

```cpp
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>

// New Pass Manager için Custom Pass Sınıfı
class GameFastMathPass : public llvm::PassInfoMixin<GameFastMathPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function& F, llvm::FunctionAnalysisManager& FAM) {
        bool changed = false;
        llvm::outs() << "GameFastMathPass Calisiyor: Fonksiyon " << F.getName() << "\n";

        for (auto& BB : F) {
            for (auto InstIt = BB.begin(); InstIt != BB.end(); ) {
                llvm::Instruction& I = *InstIt++;

                // Eğer bir FDiv (Float Bölme) işlemiyse ve bölen sabit bir sayıysa
                if (auto* fdiv = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
                    if (fdiv->getOpcode() == llvm::Instruction::FDiv) {
                        if (auto* c = llvm::dyn_cast<llvm::ConstantFP>(fdiv->getOperand(1))) {
                            // 1.0 / Constant hesabı derleme zamanında yap
                            double val = c->getValueAPF().convertToDouble();
                            if (val != 0.0) {
                                double recip = 1.0 / val;
                                llvm::IRBuilder<> builder(fdiv);
                                llvm::Value* recipVal = builder.getFloatTy()->isFloatTy() ?
                                    (llvm::Value*)llvm::ConstantFP::get(builder.getFloatTy(), recip) :
                                    (llvm::Value*)llvm::ConstantFP::get(builder.getDoubleTy(), recip);

                                // FDiv yerine FMul oluştur
                                llvm::Value* newMul = builder.CreateFMul(fdiv->getOperand(0), recipVal, "fastmul");
                                fdiv->replaceAllUsesWith(newMul);
                                fdiv->eraseFromParent();
                                changed = true;
                            }
                        }
                    }
                }
            }
        }

        // Eğer IR değiştiyse analizleri invalidate et, değişmediyse koru
        return changed ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
    }
};
```

<div class="callout callout-tip">
<div class="callout-title">İpucu: eraseFromParent() Kullanımı</div>
Bir LLVM talimatını silmeden önce (`eraseFromParent()`), onun ürettiği sonucu kullanan diğer tüm talimatların `replaceAllUsesWith()` ile yeni talimata yönlendirildiğinden emin olunmalıdır!
</div>

---

## 3.5 Pass Pipeline Oluşturma ve Optimizasyon Çalıştırma

Oyun derleyicimizde bu custom pass'ı ve LLVM'in standart optimizasyon pipeline'ını nasıl çalıştıracağımızı görelim:

```cpp
#include <llvm/Passes/PassBuilder.h>
#include <llvm/IR/PassManager.h>

void runOptimizationPipeline(llvm::Module& module) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;

    // Analiz yöneticilerini kaydet
    PB.registerModuleAnalyses(MAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerRegisterClassBuilders(CGAM, LAM, FAM, MAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Module Pass Manager
    llvm::ModulePassManager MPM;

    // Standart O2 Optimizasyon Pipeline'ını Yükle
    MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

    // Pipeline'ı Modül Üzerinde Çalıştır
    MPM.run(module, MAM);
    llvm::outs() << "=== Modul Optimizasyonlari Tamamlandi ===\n";
}
```

Bu bölüm ile LLVM Pass Manager altyapısını, New Pass Manager'ın mimarisini ve custom optimizasyon yazmayı öğrendik.
