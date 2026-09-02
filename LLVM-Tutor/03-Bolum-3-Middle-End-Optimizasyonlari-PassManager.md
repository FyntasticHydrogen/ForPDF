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

1. **Tür Güvenliği ve Derleme Zamanı Şablonları (Templates & CRTP):** Kalıtım (vtable) yerine C++ şablonları ve Mixin yapısı (`llvm::PassInfoMixin`) kullanılarak sanal fonksiyon çağrı maliyetleri sıfırlanmıştır.
2. **Esnek Analiz Önbellekleme (Analysis Management):** Bir optimizasyon passi IR'ı değiştirmediyse, önceki analiz sonuçları (örn: Dominator Tree, Loop Info) tekrar hesaplanmaz (`PreservedAnalyses::all()`).
3. **Piyasa Standartları Pipeline Desteği:** `O1`, `O2`, `O3`, `Os`, `Oz` gibi hazır optimizasyon seviyeleri tek hat üzerinden yapılandırılabilir.

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

### Koda Adım Adım Derinlemesine Bakış ve Pass Yürütme Algoritması

1. **`PassInfoMixin<GameFastMathPass>` Kalıtımı (CRTP Pattern):**
   * *Çalışma Mantığı:* Curiously Recurring Template Pattern (CRTP) yapısıdır. Sanal fonksiyon tablosu (vtable) masrafı olmadan static polymorphism sağlar.
2. **`run(Function& F, FunctionAnalysisManager& FAM)` Metodu:**
   * *Çalışma Mantığı:* NPM her fonksiyon için bu metodu otomatik çağırır. `FAM` parametresi sayesinde Dominator Tree veya Loop Info gibi analizler hazır alınabilir.
3. **Güvenli İteratör Döngüsü (`InstIt++`):**
   * *Çalışma Mantığı:* Döngü içinde bir talimat silineceği için (`eraseFromParent()`), iteratör önceden artırılır (`*InstIt++`). Aksi takdirde dangling pointer hatası ile derleyici çöker!
4. **`replaceAllUsesWith` & `eraseFromParent`:**
   * *Çalışma Mantığı:* Eski bölme talimatına bağlı olan tüm SSA tüketicilerini yeni çarpma talimatına yönlendirir ve eski talimatı bellekten siler.
5. **`PreservedAnalyses` Dönüş Değeri:**
   * *Çalışma Mantığı:* Eğer fonksiyonda değişiklik yapıldıysa `none()` dönerek diğer analizlerin tekrar hesaplanmasını sağlar; değişiklik yoksa `all()` dönerek önbelleği korur.

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

---

## 3.6 Bölüm Özeti ve Orta Yüz (Middle-end) Optimizasyon Değerlendirmesi

Bu bölümde, derleyicimizin Orta Yüz (Middle-End) katmanını ve **New Pass Manager (NPM)** mimarisini detaylıca öğrendik. C++ şablonları tabanlı NPM yapısının eski Legacy Pass Manager'a göre sağladığı hız ve analiz önbellekleme üstünlüklerini inceledik.

Oyun motorlarının yüksek başarım gereksinimleri için hayati önem taşıyan Sabit Katlama (Constant Folding), Ortak Alt İfade Eleme (CSE), Ölü Kod Eleme (DCE) ve Döngü Açma (Loop Unrolling) tekniklerinin LLVM IR üzerindeki dönüşümlerini izledik. Yavaş bölme işlemlerini çarpmaya çeviren özgün bir `GameFastMathPass` C++ sınıfı kodlayarak analiz ve dönüştürme pass'lerinin çalışma algoritmalarını kavradık. Bir sonraki bölümde, optimize edilmiş bu IR'ı gerçek donanım komutlarına (Assembly / Object File) dönüştürmek üzere **Backend Kod Üretimi ve Target Machine** mimarisine geçeceğiz.
